
#ifndef COMPILA_H_
#define COMPILA_H_
#include <stdio.h>




typedef int (*funcp) ();
funcp compila (FILE* f);
void libera (void *p);

#endif /* COMPILA_H_ */
