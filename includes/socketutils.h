#if !defined(SOCKET_UTILS_H_)
#define SOCKET_UTILS_H_

//file socket
#if !defined(SOCKNAME)
#define SOCKNAME "./farm.sck"  
#endif 

/** Evita letture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la lettura da fd leggo EOF
 *   \retval size se termina con successo
 */
static inline int readn(long fd,void *buf,size_t size){
    size_t left = size;
    int r;
    char *bufptr = (char *)buf;
    while(left > 0) {
	   if((r = read((int)fd,bufptr,left)) == -1){
        //controllo se la read è stata interrotta
	    if(errno == EINTR) 
            continue;
	    return -1;
	   }
       //EOF
	   if(r == 0) 
        return 0;
       left    -= r;
	   bufptr  += r;
    }
    return size;
}

/** Evita scritture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la scrittura la write ritorna 0
 *   \retval  1   se la scrittura termina con successo
 */
static inline int writen(long fd,void *buf,size_t size){
    size_t left = size;
    int r;
    char *bufptr = (char *)buf;
    while(left > 0){
	   if((r = write((int)fd,bufptr,left)) == -1){
           //controllo se la write è stata interrotta
	       if(errno == EINTR) 
            continue;
	       return -1;
	   }
	   if(r == 0) 
        return 0;  
       left    -= r;
	   bufptr  += r;
    }
    return 1;
}

#endif
