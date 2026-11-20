#ifndef STDLIB_H
#define STDLIB_H

#include "types.h"

/* Conversion */
int         atoi(const char* s);
long        atol(const char* s);
char*       itoa(int value, char* str, int base);
char*       utoa(uint32_t value, char* str, int base);
long        strtol(const char* s, char** endptr, int base);
unsigned long strtoul(const char* s, char** endptr, int base);

/* Pseudo-random (LCG) */
int         rand(void);
void        srand(unsigned int seed);

/* Math helpers */
int         abs(int n);
long        labs(long n);
int         atoi_base(const char* s, int base);

#define RAND_MAX  2147483647

#endif /* STDLIB_H */
