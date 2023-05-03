#if !defined(_MY_THREAD_UTILS)
#define _MY_THREAD_UTILS

//macro per facilitare la verifica della chiamata pthread_mutex_lock() con valore di ritorno
#define LOCK_WITH_CONTROL(l,r)    if(pthread_mutex_lock(l) != 0){ \
    fprintf(stderr,"errore lock\n"); \
    return r; \
}   

//macro per facilitare la verifica della chiamata pthread_mutex_unlock() con valore di ritorno
#define UNLOCK_WITH_CONTROL(l,r)    if(pthread_mutex_unlock(l) != 0){  \
    fprintf(stderr,"errore unlock\n");  \
    return r; \
}

#endif