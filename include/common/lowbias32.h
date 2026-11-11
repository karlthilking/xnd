/* lowbias32.h */
#ifndef XND_LOWBIAS32_H
#define XND_LOWBIAS32_H

#include <stdint.h>

static inline uint32_t
lowbias32 (uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

#endif /* XND_LOWBIAS32_H */
