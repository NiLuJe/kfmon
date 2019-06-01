/*	@(#)	str5cpy.c	*/
/*
 *	v 2.0, 2016/05, Eric Sanchis <eric.sanchis@iut-rodez.fr>
 *	GNU LGPL
 */

#include "str5.h"

// NOTE: Compared to the original str5 implementation, this has been modified to pad the destination buffer with zeroes,
//       like strncpy
int
    str5cpy(char* restrict dst, size_t dstsize, const char* restrict src, size_t nb, size_t mode)
{
	size_t srclen;

	if (dst == NULL || dstsize == 0)
		return EDSTPAR;
	if (src == NULL)
		return ESRCPAR;
	if (mode != TRUNC && mode != NOTRUNC)
		return EMODPAR;

	if (nb == 0) {
		memset(dst, '\0', dstsize);
		return OKNOTRUNC;
	}

	/* find the nul byte of src within the first dstsize characters */
	for (srclen = 0; srclen < dstsize && src[srclen] != '\0'; srclen++)
		;

	if (srclen == 0) {
		memset(dst, '\0', dstsize);
		return OKNOTRUNC;
	}

	nb = nb > srclen ? srclen : nb;

	if (dstsize <= nb) /* dst: not enough space */
	{
		if (mode == TRUNC) /* truncation allowed */
		{
			memcpy(dst, src, dstsize - 1);
			dst[dstsize - 1] = '\0';
			return OKTRUNC;
		} else /*  mode == NOTRUNC */
			return ETRUNC;
	}

	/* dst: enough space */
	memcpy(dst, src, nb);
	memset(dst + nb, '\0', dstsize - nb);
	return OKNOTRUNC;
}
