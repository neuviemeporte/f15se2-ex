#ifndef INTTYPE_H
#define INTTYPE_H

/*
 * Results from example code run:
 *
 * long x;
 * int a;
 * short b;
 * char c;
 * void near *pn;
 * void far *pf;
 * printf("long = %d, int = %d, short = %d, char = %d, near = %d, far = %d, size = %d\n", sizeof(x), sizeof(a), sizeof(b), sizeof(c), sizeof(pn), sizeof(pf), sizeof(size_t));
 *
 * long = 4, int =2, short = 2, char = 1, near = 2, far = 4, size = 2
 */
#include <stdint.h>
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;

#endif /* INTTYPE_H */
