#if !defined(_MY_IS_NUMB_H_)
#define _MY_IS_NUMB_H_

#include <stdlib.h>   
#include <string.h>   
#include <errno.h>    
/*
Controlla se la stringa passata come primo argomento è un numero.
Se ritorna 0 ok; 1 non è un numero; 2 overflow/underflow
*/
static int isNumber(const char* s, long* n) {
  if (s==NULL) return 1;
  if (strlen(s)==0) return 1;
  char* e = NULL;
  errno=0;
  long val = strtol(s, &e, 10);
  if (errno == ERANGE) return 2;  //overflow/underflow
  if (e != NULL && *e == (char)0) {
    *n = val;
    return 0; //successo 
  }
  return 1; //non è un numero
}

#endif