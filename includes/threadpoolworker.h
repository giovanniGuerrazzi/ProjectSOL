#if !defined(THREAD_POOL_WORKER_H_)
#define THREAD_POOL_WORKER_H_

#include <pthread.h>

// -------------------- Interfaccia ThreadPool -------------------- //

//struttura che rappresenta un generico task che un worker thread del threadpool deve eseguire
typedef struct taskfun_t {
    void (*fun)(void *);            //funzione/task
    void *arg;                      //argomenti funzione
} taskfun_t;
  
//struttura che rappresenta l'oggetto threadpool
typedef struct threadpool_t {
    pthread_t * threads;            //array thread worker
    int num_threads;                //numero di thread (di default è 4)
    taskfun_t *pending_task_queue;  //coda contenente task pendenti (array circolare)
    int queue_size;                 //massima dimensione della coda (di default è 8)
    int head;                       //indice elemento in testa alla coda dei task (prossimo task da prelevare)
    int tail;                       //indice elemento in fondo alla coda dei task (dove inserire un nuovo task)
    int task_in_queue;              //numero di task nella coda dei task pendenti
    int exiting;                    //valore che mi dice se devo terminare.Se vale 0 non devo terminare, se vale 1 termino il threadpool aspettando che non ci siano piu' lavori in coda altrimenti se = 2 uscita forzata, termino il threadpool non finendo i lavori in coda
    pthread_mutex_t  lock;          //mutex threadpool
    pthread_mutex_t  lock_fun;      //mutex threadpool
    pthread_cond_t   cond;          //variabile condizione threadpool
} threadpool_t;


/**
 * @function: createThreadPool
 * @brief: crea un oggetto threadpool
 * @param: <num_threads> è il numero dei thread del threadpool
 * @param: <queue_size> è il numero dei task che il threadpool può contenere all'interno di una coda (di default è 8)
 * @return: un oggetto threadpool se la funzione ha avuto successo,altrimenti ritorna NULL con errno settato appropriatamente
 */
threadpool_t *createThreadPool(int num_threads, int queue_size);


/**
 * @function: addFileTaskToThreadPool
 * @brief: aggiunge un task al threadpool se c'è spazio nella queue,altrimenti aspetta che si liberi lo spazio
 * @param: <pool> oggetto threadpool
 * @param: <fun> task da far eseguire al worker thread
 * @param: <arg>  argomenti della funzione
 * @return: 0 se il task è stato inserito con successo, 1 se il task non è stato inserito perchè il threadpool è in fase di uscita, -1 in caso di fallimento con errno settato appropriatamente.
 */
int addFileTaskToThreadPool(threadpool_t *pool, void (*fun)(void *),void *arg);


/**
 * @function: destroyThreadPool
 * @brief: termina tutti i thread e distrugge l'oggetto threadpool
 * @param: <pool> è l'oggetto da liberare
 * @param: <mode> se vale 1 forza l'uscita immediata di tutti i thread e libera subito le risorse, se vale 0 aspetta che i thread finiscano tutti e soli i task nella queue (non accetta altri task).
 * @return: 0 in caso di successo, -1 in caso di fallimento con errno settato appropriatamente
 */
int destroyThreadPool(threadpool_t *pool, int mode);

#endif

