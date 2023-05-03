#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
//my include
#include <threadutils.h>
#include <threadpoolworker.h>

//      --------------- Implementazione interfaccia threadPoolWorker ---------------        //

//Funzioni da implementare
threadpool_t *createThreadPool(int,int);
int addFileTaskToThreadPool(threadpool_t *,void (*f)(void *),void *);
int destroyThreadPool(threadpool_t *,int);
//funzioni aggiuntive
static void *threadWorkerFun(void *);
void freePoolResources(threadpool_t *);


//Funzione per la creazione del threadpool
threadpool_t *createThreadPool(int num_threads,int queue_size){
    //controllo che gli argomenti passati siano validi
    if(num_threads <= 0 || queue_size < 0){
        errno = EINVAL;
        return NULL;
    }

    //alloco spazio per la struttura threadpool
    threadpool_t *pool = (threadpool_t *)malloc(sizeof(threadpool_t));
    if(pool == NULL){
        fprintf(stderr,"malloc pool\n");
        return NULL;
    }

    //condizioni iniziali
    pool -> num_threads = 0;
    pool -> queue_size = queue_size;
    if(pool -> queue_size == 1)
        pool -> queue_size = 2;
    pool -> head = 0;
    pool -> tail = 0;
    pool -> task_in_queue = 0;
    pool -> exiting = 0;

    //alloco spazio per i threads del pool
    pool -> threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
    if(pool -> threads == NULL){
       fprintf(stderr,"malloc pool -> threads\n");
       free(pool);
       return NULL;
    }
    //alloco spazio per la task queue
    pool -> pending_task_queue = (taskfun_t *)malloc(sizeof(taskfun_t) * (pool -> queue_size));
    if(pool -> pending_task_queue == NULL){
       fprintf(stderr,"malloc pool -> pending_task_queue\n");
       free(pool -> threads);
       free(pool);
       return NULL;
    }
    //inizializzo il mutex
    if((pthread_mutex_init(&(pool -> lock),NULL) != 0)){
       fprintf(stderr,"pthread_mutex_init error\n");
       free(pool -> threads);
       free(pool -> pending_task_queue);
       free(pool);
       return NULL;
    }
    //inizializzo il mutex_fun
    if((pthread_mutex_init(&(pool -> lock_fun),NULL) != 0)){
       fprintf(stderr,"pthread_mutex_init error\n");
       pthread_mutex_destroy(&(pool -> lock));
       free(pool -> threads);
       free(pool -> pending_task_queue);
       free(pool);
       return NULL;
    }
    //inizializzo la varaibile condizione
    if((pthread_cond_init(&(pool -> cond),NULL) != 0)){
       fprintf(stderr,"pthread_cond_init error\n");
       pthread_mutex_destroy(&(pool -> lock));
       pthread_mutex_destroy(&(pool -> lock_fun));    
       free(pool -> threads);
       free(pool -> pending_task_queue);
       free(pool);
       return NULL;
    }
    //creo i threads del pool
    for(int i = 0;i < num_threads;i++){
        if(pthread_create(&(pool -> threads[i]),NULL,threadWorkerFun,(void *)pool) != 0){
            fprintf(stderr,"pthread_create pool -> threads[%d]\n",i);
            //libero tutto forzando l'uscita dei threads (parametro passato 1)
            destroyThreadPool(pool,1);
            errno = EFAULT;
            return NULL;
        }
        //creazione thread 'i' avvenuta con successo,aumento il numero di thread presente all'interno del pool
        pool -> num_threads++;
    }
    return pool;
}


//Funzione che aggiunge un task alla coda pendente dei task del threadpool
int addFileTaskToThreadPool(threadpool_t *pool,void (*f)(void *),void *arg){
    //controllo che gli argomenti passati siano validi
    if(pool == NULL || f == NULL){
       errno = EINVAL;
       return -1;
    }
    int r;
    LOCK_WITH_CONTROL(&(pool -> lock),-1);

    //controllo se siamo in fase di uscita: non possiamo aggiungere il task al threadpool
    if(pool -> exiting){
        UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
        fprintf(stdout,"FASE DI USCITA,non aggiungo il task alla coda\n");
        return 1;//esco con valore "fase di uscita"
    }

    //controllo se la è coda piena
    while(pool -> task_in_queue == pool -> queue_size){
        if((r = pthread_cond_wait(&(pool -> cond),&(pool -> lock))) != 0){
            fprintf(stderr,"pthread_cond_wait error\n");
            UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
            errno = r;
            return -1;
        }
    }

    //possibile aggiungere il task alla pending_task_queue
    if(pool -> tail == pool -> queue_size) 
        //resetto l'indice di coda della queue
        pool -> tail = 0;
    pool -> pending_task_queue[pool -> tail].fun = f;
    pool -> pending_task_queue[pool -> tail].arg = arg;
    pool -> task_in_queue++;    
    pool -> tail++;
    
    //aggiunto task alla coda,verifico se c'è un thread del threadpool in attesa,nel caso lo risveglio
    if((r = pthread_cond_signal(&(pool -> cond))) != 0){
        fprintf(stderr,"pthread_cond_signal error\n");
        UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
        errno = r;
        return -1;
    }
    
    UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
    return 0;
}


//Funzione che elimina il threadpool liberando la memoria
int destroyThreadPool(threadpool_t *pool,int mode){
    //controllo che gli argomenti passati siano validi    
    if(pool == NULL || mode < 0){
       errno = EINVAL;
       return -1;
    }

    LOCK_WITH_CONTROL(&(pool -> lock),-1);

    /*
    *guardo come devo terminare il threadpool:
    *se mode == 0 allora pool -> exiting vale 1 e termino una volta finiti i task in queue 
    *se mode == 1 allora pool -> exiting vale 2 e termino subito 
    */
    pool -> exiting = 1 + mode;

    //sveglio tutti i thread in attesa sulla variabile condizione
    if(pthread_cond_broadcast(&(pool -> cond)) != 0){
        fprintf(stderr,"pthread_cond_broadcast error\n");
        UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
        errno = EFAULT;
        return -1;
    }

    UNLOCK_WITH_CONTROL(&(pool -> lock),-1);

    //attendo la terminazione di tutti i thread
    for(int i = 0;i < pool -> num_threads;i++){
       if(pthread_join(pool -> threads[i],NULL) != 0){
           fprintf(stderr,"pthread_join pool -> threads[%d]\n",i);
           errno = EFAULT;
           UNLOCK_WITH_CONTROL(&(pool -> lock),-1);
           return -1;
       }
    }
    //thread terminati posso liberare il pool
    freePoolResources(pool);
    return 0;
}


//Funzione eseguita dal worker thread che appartiene al threadpool
static void *threadWorkerFun(void *threadpool){    
    threadpool_t *pool = (threadpool_t *)threadpool;
    taskfun_t task;     //generic task
    int r;
    //The  pthread_self() function returns the ID of the calling thread.Always succeeds.
    pthread_t self = pthread_self();
    int myid = -1;
    //cerco quale thread del threadpool sono
    do{
	   for(int i = 0;i < pool -> num_threads;i++){
            //The pthread_equal() function compares two thread identifiers.
	       if(pthread_equal(pool->threads[i],self)){
	           myid = i;
	           break;
	       }
       }
    }while (myid < 0);

    //sono il thread 'i' dell'array dei thread del pool
    LOCK_WITH_CONTROL(&(pool->lock),NULL);
    while(true){
        //se non ci sono task da eseguire e non devo terminare aspetto
        while((pool -> task_in_queue == 0) && (!pool -> exiting)){
            if((r = pthread_cond_wait(&(pool -> cond),&(pool -> lock))) != 0){
                fprintf(stderr,"pthread_cond_signal error\n");
                UNLOCK_WITH_CONTROL(&(pool -> lock),NULL);
                errno = r;
                return NULL;
            }
	      }
        //controllo se devo terminare
        if(pool -> exiting == 2)
                break;
        if(pool -> exiting == 1 && !pool -> task_in_queue) 
                break;  

        //possibile prelevare un task dalla pending_task_queue
        if(pool -> head == pool -> queue_size) 
            //resetto l'indice di testa della queue
            pool -> head = 0;
	    //il thread esegue un nuovo task(lo prende dalla testa della coda)
        task.fun = pool -> pending_task_queue[pool -> head].fun;
        task.arg = pool -> pending_task_queue[pool -> head].arg;

        pool -> head++; 
        pool -> task_in_queue--;
        
        //rimosso task dalla coda,verifico se c'è un thread del threadpool in attesa di aggiungere un task nuovo,nel caso lo risveglio
        if((r = pthread_cond_signal(&(pool -> cond))) != 0){
            fprintf(stderr,"pthread_cond_signal error\n");
            UNLOCK_WITH_CONTROL(&(pool -> lock),NULL);
            errno = r;
            return NULL;
        }
        UNLOCK_WITH_CONTROL(&(pool -> lock),NULL);

        LOCK_WITH_CONTROL(&(pool -> lock_fun),NULL);
        //il worker thread esegue il task
        (*(task.fun))(task.arg);
	    UNLOCK_WITH_CONTROL(&(pool -> lock_fun),NULL);

	    LOCK_WITH_CONTROL(&(pool -> lock),NULL);
    }
    
    UNLOCK_WITH_CONTROL(&(pool -> lock),NULL);

    return NULL;
}


//Funzione di supporto per liberare il threadpool
void freePoolResources(threadpool_t *pool){
    if(pool -> threads)
        //dealloco la memoria occupata dall'array dei threads
        free(pool -> threads);
    if(pool -> pending_task_queue)
        //dealloco la memoria della coda dei task
        free(pool -> pending_task_queue);
    //"distruggo" i mutex
    int r1 = pthread_mutex_destroy(&(pool -> lock));
    if(r1 != 0){
        errno = r1;
        perror("pthread_mutex_destroy pool -> lock");
    }
    int r2 = pthread_mutex_destroy(&(pool -> lock_fun));
    if(r2 != 0){
        errno = r2;
        perror("pthread_mutex_destroy pool -> lock_fun");
    }
    //"distruggo" la variabile condizione
    int r3 = pthread_cond_destroy(&(pool -> cond));
    if(r3 != 0){
        errno = r3;
        perror("pthread_cond_destroy pool -> cond");
    }
    if(pool)
        //dealloco la memoria occupata dal threadpool
        free(pool);    
}
