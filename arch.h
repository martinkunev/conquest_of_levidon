#if !defined(_ARCH_H)
# define _ARCH_H

#include <sys/types.h>

#if (BYTE_ORDER == BIG_ENDIAN)
# define ENDIAN_BIG
#elif (BYTE_ORDER == LITTLE_ENDIAN)
# define ENDIAN_LITTLE
#else
#  error "Unable to detect endianness."
#endif

// Simple endian conversion.
#include <string.h>
static inline void endian_swap16(void *restrict to, const void *restrict from)
{
	const char *src = from;
	char *dest = to;
	dest[0] = src[1]; dest[1] = src[0];
}
static inline void endian_swap32(void *restrict to, const void *restrict from)
{
	const char *src = from;
	char *dest = to;
	dest[0] = src[3]; dest[1] = src[2];
	dest[2] = src[1]; dest[3] = src[0];
}
static inline void endian_swap64(void *restrict to, const void *restrict from)
{
	const char *src = from;
	char *dest = to;
	dest[0] = src[7]; dest[1] = src[6];
	dest[2] = src[5]; dest[3] = src[4];
	dest[4] = src[3]; dest[5] = src[2];
	dest[6] = src[1]; dest[7] = src[0];
}

#if defined(ENDIAN_BIG)
# define endian_big16(dest, src) memcpy((dest), (src), 2)
# define endian_big32(dest, src) memcpy((dest), (src), 4)
# define endian_big64(dest, src) memcpy((dest), (src), 8)
# define endian_little16(dest, src) endian_swap16((dest), (src))
# define endian_little32(dest, src) endian_swap32((dest), (src))
# define endian_little64(dest, src) endian_swap64((dest), (src))
#else /* defined(ENDIAN_LITTLE) */
# define endian_big16(dest, src) endian_swap16((dest), (src))
# define endian_big32(dest, src) endian_swap32((dest), (src))
# define endian_big64(dest, src) endian_swap64((dest), (src))
# define endian_little16(dest, src) memcpy((dest), (src), 2)
# define endian_little32(dest, src) memcpy((dest), (src), 4)
# define endian_little64(dest, src) memcpy((dest), (src), 8)
#endif

#endif /* !defined(_ARCH_H) */
