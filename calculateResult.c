#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
//my include
#include <generalutils.h>
#include <socketutils.h>

//struttura che appresenta gli argomenti da passare al threadpool
typedef struct{
	long	conn_fd;		//file decriptor per connessione tramite socket
	char 	*filepath;		//filepath del file
	long	stop; 			//variabile che ci dice se devo terminare
}threadPoolArgs_t;


//Funzione eseguita da un generico worker thread del threadpool
void calculateResult(void *arg) {

    long conn_fd 	= ((threadPoolArgs_t *)arg) -> conn_fd;

    char *filepath 	= ((threadPoolArgs_t *)arg) -> filepath;

    long stop 		= ((threadPoolArgs_t *)arg) -> stop;

    free(arg);

    if(stop == 1){
     	free(filepath);
     	return;
    }
    
    long n_elem;	//in un file binario il numero di elementi è uguale al numero di bytes / 8
	long n_byte;
	int fd;
	//si apre il file in lettura/scrittura
	SYSCALL(fd,open(filepath,O_RDWR),"open");

	struct stat file_info;
	_SYSCALL_(fstat(fd,&file_info),"stat");

	n_byte = (long)file_info.st_size;//dimensioni in bytes del file
	n_elem = n_byte / 8;

	/*
  	*mmap() creates a new mapping in the virtual address space of the calling process. The
  	*starting address for the new mapping is specified in addr.The length argument specifies
  	*the length of the mapping.
  	*/
  	long *p = mmap(NULL,n_elem * sizeof(long),PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
  	_SYSCALL_(close(fd),"close fd");
  	if(p == MAP_FAILED){
    	perror("mmap");
    	exit(errno);
  	}
  	long result = 0;
  	long *q = p;
  	for(long i=0;i<n_elem;i++) {
    	result += i * *q;
    	q++;
  	}
  	/*
  	*The munmap() system call deletes the mappings for the specified address range, and causes
  	*further references to addresses within the range to generate invalid memory references.
  	*The region is also automatically unmapped when the process is terminated.On the other
  	*hand, closing the file descriptor does not unmap the region.
  	*/
  	munmap(p,n_elem * sizeof(long));

  	long len_filepath = strlen(filepath);

  	_SYSCALL_(writen(conn_fd,&result,sizeof(long)),"writen result");
  	_SYSCALL_(writen(conn_fd,&len_filepath,sizeof(long)),"writen len_filepath");
  	_SYSCALL_(writen(conn_fd,filepath,sizeof(char) * len_filepath),"writen filepath");

  	free(filepath);   

}
