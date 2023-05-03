#if !defined(GENERAL_UTILS_H_)
#define GENERAL_UTILS_H_

#include <stdlib.h>   
#include <string.h>   
#include <errno.h>   

//macro per facilitare la verifica delle system call
#define SYSCALL(res,sys_call,err_msg)   if((res=sys_call)==-1)  { perror(err_msg);exit(errno); }
#define _SYSCALL_(sys_call,err_msg)     if((sys_call)==-1)      { perror(err_msg);exit(errno); }

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#endif