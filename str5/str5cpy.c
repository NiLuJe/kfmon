/*	@(#)	str5cpy.c	*/
/*
 *	v 2.0, 2016/05, Eric Sanchis <eric.sanchis@iut-rodez.fr>
 *	SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "str5.h"

// NOTE: Compared to the original str5 implementation, this has been modified to pad the destination buffer with zeroes,
//       like strncpy
int
    str5cpy(char* restrict dst, size_t dstsize, const char* restrict src, size_t nb, size_t mode)
{
	size_t srclen = 0U;

	if (dst == NULL || dstsize == 0U) {
		return EDSTPAR;
	}
	if (src == NULL) {
		return ESRCPAR;
	}
	if (mode != TRUNC && mode != NOTRUNC) {
		return EMODPAR;
	}

	if (nb == 0U) {
		memset(dst, '\0', dstsize);
		return OKNOTRUNC;
	}

	/* find the nul byte of src within the first dstsize characters */
	while (srclen < dstsize && src[srclen] != '\0') {
		srclen++;
	}

	if (srclen == 0U) {
		memset(dst, '\0', dstsize);
		return OKNOTRUNC;
	}

	nb = nb > srclen ? srclen : nb;

	/* dst: not enough space */
	if (dstsize <= nb) {
		if (mode == TRUNC) {
			/* truncation allowed */
			memcpy(dst, src, dstsize - 1U);
			dst[dstsize - 1U] = '\0';
			return OKTRUNC;
		} else {
			/*  mode == NOTRUNC */
			return ETRUNC;
		}
	}

	/* dst: enough space */
	memcpy(dst, src, nb);
	memset(dst + nb, '\0', dstsize - nb);
	return OKNOTRUNC;
}
