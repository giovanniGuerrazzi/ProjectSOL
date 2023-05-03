#define _POSIX_C_SOURCE 2001112L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
//my include
#include <generalutils.h>
#include <isnumber.h>
#include <threadutils.h>
#include <socketutils.h>
#include <threadpoolworker.h>
#include <calculateresult.h>

//valori di default
#define NTHREAD 4
#define QLEN	8
#define DELAY	0
//dimensione massima filepath
#define MAX_LEN_PATH 256

//variabili globali
volatile sig_atomic_t QUIT 			= 0;	//terminazione per aver ricevuto segnale SIGHUP/SIGINT/SIGQUIT/SIGTERM
volatile sig_atomic_t STOP 			= 0;	//terminazione per aver ricevuto segnale SIGUSR1
volatile sig_atomic_t termina 		= 0;	//variabile che mi segnala di terminare nel mentre che sto analizzando i file (fuori dalla directory)
volatile sig_atomic_t dir_termina 	= 0;	//variabile che mi segnala di terminare nel mentre che sto analizzando i file (dentro la directory)
int END 							= 0;	//terminazione standard senza segnali

//struttura che appresenta gli argomenti da passare al thread pool
typedef struct{
	long	conn_fd;		//file decriptor per connessione tramite socket
	char 	*filepath;		//filepath del file
	long	stop; 			//variabile che ci dice se devo terminare
}threadPoolArgs_t;

//struttura che mi contiene i risultati della funzione calculateResult associati al filepath relativo
typedef struct collection_t{
	long result;
	char *filepath;
	struct collection_t *next;
}collection_t;

//prototipi funzione
void sigHandler(int);
void insertOrder(collection_t **,collection_t *);
void printList(collection_t *);
void freeList(collection_t *);
void explore_directory(char *,threadpool_t *,long,struct timespec,struct timespec);
int isDirectory(const char *);
int isdot(const char *);


int main(int argc,char *argv[]){
	//		--------------- PROCESSO MASTER WORKER ---------------		//
	
	//controllo argomenti passati da input
	if(argc == 1){
		fprintf(stderr,"Usage: %s -n <nthread> -q <qlen> -t <delay> file1.dat1 file2.dat ... list_of_file.dat -d <directory>\n",argv[0]);
		fprintf(stderr,"Immettere almeno un file binario o una directory,tutti gli altri comandi sono opzionali\n");
		fprintf(stderr,"-n ---> thread che compongono il threadpool\t(di default se mancante è 4)\n");
		fprintf(stderr,"-q ---> dimensione della coda dei task      \t(di default se mancante è 8)\n");
		fprintf(stderr,"-t ---> tempo in millisecondi di ritardo    \t(di default se mancante è 0)\n");
		return -1;
	}

  	//		--------------- preparativi socket generali ---------------		//
 	int fd_skt;
  	struct sockaddr_un socket_address;
  	//inizializzo a 0 la memoria della struttura
  	memset(&socket_address,'0',sizeof(socket_address));
 	socket_address.sun_family = AF_UNIX;    //dominio
  	strncpy(socket_address.sun_path,SOCKNAME,strlen(SOCKNAME) + 1);
  	//		--------------- fine preparativi socket generali---------------		//

	pid_t pid;
	//creo il processo Collector
	SYSCALL(pid,fork(),"fork");

	if(pid == 0){//collector(processo figlio)
		//		--------------- PROCESSO COLLECTOR ---------------		//

		//		--------------- gestione segnali ---------------		//
		struct sigaction s;
		//inizializzo la struttura a 0
 		memset(&s,0,sizeof(s));
 		s.sa_handler = 	SIG_IGN;
 		_SYSCALL_(sigaction(SIGHUP,&s,NULL),	"sigaction SIGHUP");
 		_SYSCALL_(sigaction(SIGINT,&s,NULL),	"sigaction SIGINT");
 		_SYSCALL_(sigaction(SIGQUIT,&s,NULL),	"sigaction SIGQUIT");
 		_SYSCALL_(sigaction(SIGTERM,&s,NULL),	"sigaction SIGTERM");
 		_SYSCALL_(sigaction(SIGUSR1,&s,NULL),	"sigaction SIGUSR1");
 		_SYSCALL_(sigaction(SIGPIPE,&s,NULL),	"sigaction SIGPIPE");
 		//		--------------- fine gestione segnali ---------------		//

		//		--------------- preparativi socket lato Collector ---------------		//
  		//socket() creates an endpoint for communication and returns a descriptor.
 		SYSCALL(fd_skt,socket(AF_UNIX,SOCK_STREAM,0),"socket");
 		//		--------------- fine preparativi socket  lato Collector ---------------		//

		int byte_read;
		long result;
		long len_filepath;
		char* filepath 		= NULL;		//puntatore filepath
		collection_t *head 	= NULL;		//puntatore testa lista di risultati
		collection_t *elem 	= NULL;		//puntatore elemento creato da aggiungere alla lista

		//The connect() system call connects the socket referred to the address specified by addr.
		while(connect(fd_skt,(struct sockaddr *)&socket_address,sizeof(socket_address)) == -1){
			//il file socket non esiste ancora o il MasterWorker non si è messo ancora in ascolto
			if(errno == ENOENT || errno == ECONNREFUSED)
				sleep(1);
			else{
				perror("connect");
				exit(EXIT_FAILURE);
			}
		}

		//connessione MasterWorker e Collector avvenuta
		while(!END && !QUIT && !STOP){
	
			SYSCALL(byte_read,readn(fd_skt,&result,sizeof(long)),"readn result");
			if(byte_read == 0)//end_of_file
				break;

			SYSCALL(byte_read,readn(fd_skt,&len_filepath,sizeof(long)),"readn len_filepath");
			if(byte_read == 0)//end_of_file
				break;

			filepath = (char *)malloc(sizeof(char) * len_filepath + 1);
			if(filepath == NULL){
				fprintf(stderr,"malloc filepath\n");
				exit(EXIT_FAILURE);
			}

			SYSCALL(byte_read,readn(fd_skt,filepath,sizeof(char) * len_filepath),"readn filepath");

			if(byte_read == 0)//end_of_file
				break;

			filepath[len_filepath] = '\0';

			elem = (collection_t *)malloc(sizeof(collection_t));
			if(elem == NULL){
				fprintf(stderr,"malloc elem\n");
				exit(EXIT_FAILURE);
			}
			elem -> result = result; 
			elem -> filepath = filepath; 
			elem -> next = NULL;

			insertOrder(&head,elem);

		}//end while

		printList(head);

		freeList(head);

		_SYSCALL_(close(fd_skt),"close");

		exit(EXIT_SUCCESS);
	}//end if(pid==0)
	//		--------------- fine PROCESSO COLLECTOR ---------------		//

	if(pid){//MasterWorker(padre)
		//		--------------- PROCESSO MASTER WORKER ---------------		//

		//		--------------- gestione segnali ---------------		//
		struct sigaction s;
		//inizializzo la struttura a 0
 		memset(&s,0,sizeof(s));
 		s.sa_handler = sigHandler;
 		s.sa_flags = 	SA_RESTART;
 		_SYSCALL_(sigaction(SIGHUP,&s,NULL),	"sigaction SIGHUP");
 		_SYSCALL_(sigaction(SIGINT,&s,NULL),	"sigaction SIGINT");
 		_SYSCALL_(sigaction(SIGQUIT,&s,NULL),	"sigaction SIGQUIT");
 		_SYSCALL_(sigaction(SIGTERM,&s,NULL),	"sigaction SIGTERM");
 		_SYSCALL_(sigaction(SIGUSR1,&s,NULL),	"sigaction SIGUSR1");
 		_SYSCALL_(sigaction(SIGPIPE,&s,NULL),	"sigaction SIGPIPE");
 		//		--------------- fine gestione segnali ---------------		//

 	 	//		--------------- preparativi socket Master Worker ---------------		//
 	 	int fd_sck;
  		//socket() creates an endpoint for communication and returns a descriptor.
 		SYSCALL(fd_sck,socket(AF_UNIX,SOCK_STREAM,0),"socket");
  		//bind() assigns the address specified by addr to the socket referred to by the file descriptor sockfd.
  		_SYSCALL_(bind(fd_sck,(struct sockaddr *)&socket_address,sizeof(socket_address)),"bind");
  		/*
  		*listen() marks the socket referred as a passive socket, that is, as a socket that will be used to accept incoming connection requests using accept().
  		*SOMAXCONN è il massimo numero di richieste accodabili
  		*/
  		_SYSCALL_(listen(fd_sck,SOMAXCONN),"listen");
   		//		--------------- fine preparativi socket Master Worker ---------------		//

		//processo composto da 1 thread master e n thread worker
		long nthread 		= -1;
		long qlen 			= -1;
		long delay 			= -1;
		int dir 			=  0;
		char *directory 	=  NULL;
    	int res;
		int opt;

	  	/* 
	  	*se il primo carattere della optstring è ':' allora getopt ritorna ':', qualora non ci fosse l'argomento per le opzioni che lo richiedono.
	  	*se si incontra un'opzione (cioe' un argomento che inizia con '-') e tale opzione non e' in optstring, allora getopt ritorna '?'
	  	*optstring is a string containing the legitimate option characters.If such a character is followed by a colon, the option requires an argument
	  	*/
	  	while ((opt = getopt(argc,argv,":n:q:t:d:")) != -1){
	    	switch(opt){
	    		case 'n':{
	    			res = isNumber(optarg,&nthread);
	    			if(res == 1){
	    				fprintf(stderr,"\"%s\" non è un numero!\n",optarg);
	    				fprintf(stderr,"Uso valore di -n di default (4)\n");
	    			}
	    			if(res == 2){
	    				fprintf(stderr,"\"%s\" causa OVERFLOW/underflow!\n",optarg);
	    				fprintf(stderr,"Uso valore di -n di default (4)\n");
	    			}
	    			if(res == 0 && nthread <= 0){
	    				fprintf(stderr,"-n deve avere come parametro un valore maggiore di 0\n");
	    				fprintf(stderr,"Uso valore di -n di default (4)\n");
	    				nthread = -1;
	    			}
	    		} 
	    		break;
	    		case 'q':{
	    			res = isNumber(optarg,&qlen);
	    			if(res == 1){
	    				fprintf(stderr,"\"%s\" non è un numero!\n",optarg);
	    				fprintf(stderr,"Uso valore di -q di default (8)\n");
	    			}
	    			if(res == 2){
	    				fprintf(stderr,"\"%s\" causa OVERFLOW/underflow!\n",optarg);
	    				fprintf(stderr,"Uso valore di -q di default (8)\n");
	    			}  
	    			if(res == 0 && qlen <= 0){
	    				fprintf(stderr,"-q deve avere come parametro un valore maggiore di 0\n");
	    				fprintf(stderr,"Uso valore di -q di default (8)\n");
	    				qlen = -1;
	    			}
	    		} 
	    		break;
	    		case 't':{
	    			res = isNumber(optarg,&delay);
	    			if(res == 1){
	    				fprintf(stderr,"\"%s\" non è un numero!\n",optarg);
	    				fprintf(stderr,"Uso valore di -t di default (0)\n");
	    			}
	    			if(res == 2){
	    				fprintf(stderr,"\"%s\" causa OVERFLOW/underflow!\n",optarg);
	    				fprintf(stderr,"Uso valore di -t di default (0)\n");
	    			} 
	    			if(res == 0 && delay < 0){
	    				fprintf(stderr,"-t deve avere come parametro un valore non negativo\n");
	    				fprintf(stderr,"Uso valore di -t di default (0)\n");
	    				delay = -1;
	    			} 
	    		} 
	    		break;
	    		case 'd':{
	    			if(isDirectory(optarg)){
	    				dir = 1; 
	    				directory = optarg; 
	    			}
	    		} 
	    		break;
	    		case ':':{
	      			printf("l'opzione '-%c' richiede un argomento!\n",optopt);
	    		} 
	    		break;
	    		//restituito se getopt trova una opzione non riconosciuta
	    		case '?':{
	      			printf("l'opzione '-%c' non e' gestita!\n",optopt);
	    		} 
	    		break;
	    		default:;
	    	}
	  	}

	  	/*
	  	*all command-line options have been parsed
	  	*If there are no more option characters, getopt() returns -1.Then optind is the index in
	    *argv of the first argv-element that is not an option. (i file.dat)
	  	*verifico se non sono stati passati valori da terminale con conseguente utilizzo dei valori di default
	  	*/
	  	if(nthread == -1)
	  		nthread = NTHREAD;
	  	if(qlen == -1)
	  		qlen = QLEN;
	  	if(delay == -1)
	  		delay = DELAY;

	  	//preparo il ritardo che intercorre tra due richieste successive ai worker thread 
	  	struct timespec req;
	  	struct timespec rem;
	  	//The value of the nanoseconds field must be in the range 0 to 999999999
	  	if(delay >= 1000){
	  		long sec = delay / 1000;
	  		long millisec = delay % 1000;
	  		req.tv_sec = sec;
	  		req.tv_nsec = millisec * 1000;
	  	}
	  	else{
	  		req.tv_sec = 0;
	  		req.tv_nsec = delay * 1000000;
	  	}

	  	//thread pool
	  	threadpool_t *pool = NULL;
	  	//creo il thred pool con n thread
	  	pool = createThreadPool(nthread,qlen); 
	  	if(pool == NULL){
	    	perror("createThreadPool");
	    	exit(errno);
	  	}

	  	long conn_fd;
	  	int r;			//variabile che mi dice se aggiungo correttamente un task al threadPool
	  	int res_nanos; 

	    /*
	    *accept() extracts the first connection request on the queue of pending connections
	    *for the listening socket, sockfd, creates a new connected socket and returns a NEW 
	    *file descriptor referring to that socket.   
	    *If  no  pending connections are present on the queue, and the socket is not marked as non‐
	    *blocking, accept() blocks the caller until a connection is  present.
	    */
	    SYSCALL(conn_fd,accept(fd_sck,(struct sockaddr*)NULL,NULL),"accept");

	    //connessione con Collector avvenuta
	  	while(!termina && !END && !QUIT && !STOP){

	    	//verifico se ho passato una directory come argomento da terminale
				if(dir)
					explore_directory(directory,pool,conn_fd,req,rem);
				
	  		//per ogni file 
	    	while(argv[optind] != NULL && !QUIT && !STOP){

	    		char *file_duplicato = NULL;

				struct stat file_info;
				//controllo effetivamente se è un file
				if(stat(argv[optind],&file_info) == -1){
					perror("stat file");
					fprintf(stderr,"\"%s\" non è un file!\n",argv[optind]);
					optind++;
					continue;//continuo con while
				}
				//verifico se sto analizzando un file regolare
				if(S_ISREG(file_info.st_mode)){
					threadPoolArgs_t *args = (threadPoolArgs_t *)malloc(sizeof(threadPoolArgs_t));
					if(args == NULL){
						fprintf(stderr,"malloc args");
						exit(EXIT_FAILURE);
					}

					file_duplicato = strndup(argv[optind],MAX_LEN_PATH - 1);

					args -> conn_fd = conn_fd;

					args -> filepath = file_duplicato;

					args -> stop = (long)termina;
					
					//devo passare il nome del file ad un generico worker thread del pool
					r = addFileTaskToThreadPool(pool,calculateResult,(void *)args);
					
					res_nanos = nanosleep(&req,&rem);
					if(res_nanos == -1){
						if(errno == EINTR)
							nanosleep(&rem,NULL);
						else{
							perror("nanosleep");
							exit(errno);
						}
					}
			
					//errore aggiunta task al threadpool
					if(r == -1){
						fprintf(stderr,"ERRORE FATALE: aggiungendo un task al threadpool\n");
						fprintf(stderr,"ERRORE FATALE: non è stato possibile aggiungere il task\n");
					}
					//threadpool in terminazione
					if(r == 1){
						fprintf(stderr, "threadpool in uscita!\n");
						fprintf(stderr, "non è stato possibile aggiungere il task in quanto il threadpool sta terminando\n");
					}
				}
				//non un file regolare
				else{
					fprintf(stderr,"\"%s\" non è un file regolare!\n",argv[optind]);
				}
				optind++;
	    	}

	    	//terminazione standard senza aver ricevuto alcun segnale
	    	END = 1;

	    }

		//termino il threadpool
    	if(STOP)
    		destroyThreadPool(pool,1);
    	if(QUIT)
    		destroyThreadPool(pool,0);  
		if(END && !STOP && !QUIT)
			destroyThreadPool(pool,0);

  		_SYSCALL_(close(conn_fd),"close");
 	 	_SYSCALL_(close(fd_sck),"close");
	}//end if(pid)

  	//Master Worker aspetta la terminazione di Collector
  	_SYSCALL_(waitpid(pid,NULL,0),"waitpid");

	//rimuovo il file socket
	_SYSCALL_(unlink(SOCKNAME),"unlink");
	return 0;
}

//Signalhandler
void sigHandler (int signal){
	switch(signal){
		case SIGHUP: 	{QUIT = 1;termina = 1;dir_termina = 1;}
			break;
		case SIGINT:	{QUIT = 1;termina = 1;dir_termina = 1;}
			break;
		case SIGQUIT:	{QUIT = 1;termina = 1;dir_termina = 1;}
			break;
		case SIGTERM:	{QUIT = 1;termina = 1;dir_termina = 1;}
			break;
		case SIGUSR1:	{STOP = 1;termina = 1;dir_termina = 1;}
			break;
		case SIGPIPE:
			break;
		default:;
	}
}


//Funzione che inserisce l'elemento passato nella lista in maniera ordinata
void insertOrder(collection_t **head,collection_t *elem){

	int insert = 0;
	collection_t *curr = *head;
	collection_t *prec = NULL;
	//se lista vuota inserisco in testa
	if(curr == NULL)
		*head = elem;
	//lista non vuota
	else{
		//finche non finisco di scorrere la lista o non ho inserito al giusto posto
		while(curr != NULL && !insert){
			if((elem -> result) <= (curr -> result)){
				//inserisco in testa alla lista
				if(prec == NULL){
					elem -> next = *head;
					*head = elem;
					insert = 1;
				}
				//inserisco in "mezzo" alla lista
				else{
					prec -> next = elem;
					elem -> next = curr; 
					insert = 1;
				}
			}
			//scorro in avanti
			prec = curr;
			curr = curr -> next;
		}
		//inserisco in fondo alla lista
		if(!insert)
			prec -> next = elem;
	}
}


//Funzione che stampa la lista passata come argomento
void printList(collection_t *head){

	collection_t *index = head;
	while(index != NULL){
		printf("%ld\t",index -> result);
		printf("%s \n",index -> filepath);
		index = index -> next;
	}
}


//Funzione che libera la memoria allocata dalla lista
void freeList(collection_t *head){
	collection_t *index = head;
	while(head != NULL){
		head = head -> next;
		free(index -> filepath);
		free(index);
		index = head;
	}
}


//Funzione che esplora la directory cercando eventuali file binari
void explore_directory(char *dir_path,threadpool_t *pool,long conn_fd,struct timespec req,struct timespec rem){
	//verifico se devo terminare perchè ho ricevuto un segnale
	if(dir_termina)
		return;
	struct dirent* file;
	DIR* fd_d;//file descriptor directory
	fd_d = opendir(dir_path);
	if(!fd_d){
		perror("opendir");
		exit(errno);
	}
	while(errno = 0, (file = readdir(fd_d)) != NULL && !QUIT && !STOP){
		char path[MAX_LEN_PATH];//qui conterrò il path (che mano a mano che visito la directory trovando altre directory sarà sempre più grande)
	  	char *filepath_duplicato = NULL;
	  	//inizializzo la memoria a 0
	  	memset(path,'\0',sizeof(char) * MAX_LEN_PATH);

		//lunghezza del path fino ad ora
		long len1 = strlen(dir_path);
		//lunghezza nome del file/directory nuovo
		long len2 = strlen(file->d_name);
		//verifico se la capacità è abbastanza grande da contenere tutto il path
		if((len1 + len2 + 2) > MAX_LEN_PATH){
			fprintf(stderr, "Attenzione: superata la lunghezza massima del nome del file (compreso il pathname) di 255 (MAX_LEN_PATH)\n");
				//chiudo directory
				if(closedir(fd_d) == -1){
					perror("closedir");
					exit(errno);
				}
				exit(EXIT_FAILURE);
		}	
		//creo il path del file corrente    
		strncpy(path,dir_path,MAX_LEN_PATH - 1);

		strncat(path,"/",MAX_LEN_PATH - 1);

		strncat(path,file->d_name,MAX_LEN_PATH - 1);

		filepath_duplicato = strndup(path,MAX_LEN_PATH - 1);

		//struttura che conterrà le informazioni sul file															 		
		struct stat file_info;
		_SYSCALL_(stat(path,&file_info),"stat file in explore_directory");
		//verifico se è una directory
		if(S_ISDIR(file_info.st_mode)){
			if(!isdot(path)){//mi assicuro non sia ne la directory "." ne la directory ".."
				explore_directory(path,pool,conn_fd,req,rem);
				free(filepath_duplicato);
			}
			else
				free(filepath_duplicato);
		}
		//verifico se è un file regolare
		if(S_ISREG(file_info.st_mode)){
			int r;	//variabile che mi dice se aggiungo correttamente un task al threadPool
			int res_nanos;
			threadPoolArgs_t *args = (threadPoolArgs_t *)malloc(sizeof(threadPoolArgs_t));
			if(args == NULL){
				fprintf(stderr,"malloc args");
				exit(EXIT_FAILURE);
			}
			args -> conn_fd = conn_fd;

			args -> filepath = filepath_duplicato;

			args -> stop = dir_termina;

			//devo passare il nome del file ad un generico worker thread del pool
			r = addFileTaskToThreadPool(pool,calculateResult,(void *)args);
			res_nanos = nanosleep(&req,&rem);
			if(res_nanos == -1){
				if(errno == EINTR)
					nanosleep(&rem,NULL);
				else{
					perror("nanosleep");
					exit(errno);
				}
			}
			//errore aggiunta task al threadpool
			if(r == -1){
				fprintf(stderr,"ERRORE FATALE: aggiungendo un task al threadpool\n");
				fprintf(stderr,"ERRORE FATALE: non è stato possibile aggiungere il task\n");
			}
			//threadpool in terminazione
			if(r == 1){
				fprintf(stderr, "threadpool in uscita!\n");
				fprintf(stderr, "non è stato possibile aggiungere il task in quanto il threadpool sta terminando\n");
			}
		}
	}//verifico se sono uscito dal while a causa di un errore invece che per via della fine dei file
	if(errno != 0){//significa che c'è stato un errore nella readdir
		perror("readdir");
		exit(errno);
	}
	//chiudo directory
	if(closedir(fd_d) == -1){
				perror("closedir");
				exit(errno);
	}
}

/*
*Funzione che controlla se l'argomento passato come parametro è una directory o meno.
*ritorna 1 se è una directory
*ritorna 0 altrimenti
*/
int isDirectory(const char *file){

  struct stat info_file;
  //salvo le informazioni del file nella struttura info_file
  if(stat(file,&info_file) == -1){
  	perror("stat in isDirectory");
  	fprintf(stderr,"\"%s\" non è una directory!\n",file);
  	return 0;
  }
  if(S_ISDIR(info_file.st_mode))//verifico se è una directory
      return 1;
  else{
  		fprintf(stderr,"\"%s\" non è un una directory\n",file);
      return 0;
  }
}

/*
*Funzione che controlla se la directory passata è la directory "." o ".."
*ritorna 1 se la directory passata è la directory "." o ".." , 
*ritorna 0 altrimenti
*/
int isdot(const char *dir){
  int l = strlen(dir);
  if ((l > 0 && dir[l-1] == '.')) 
  	return 1;//directory . o ..
  return 0;//directory normale
}
