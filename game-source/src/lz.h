#ifndef LZ_H
#define LZ_H
#include <stdint.h>
unsigned lz_word(const uint8_t *p);
unsigned lz_decode(const uint8_t *src,void *dst,unsigned capacity);
unsigned lz_block(const uint8_t *src,unsigned index,void *dst,unsigned capacity);
#endif
