/*	@(#)	str5cat.c	*/
/*
 *	v 2.0, 2016/05, Eric Sanchis <eric.sanchis@iut-rodez.fr>
 *	GNU LGPL
 */

#include "str5.h"

int
    str5cat(char* dst, size_t dstsize, const char* src, size_t nb, size_t mode)
{
	size_t srclen;
	size_t dstlen;
	size_t remain;
	size_t i;
	char*  p;

	if (dst == NULL || dstsize == 0)
		return EDSTPAR;
	if (src == NULL)
		return ESRCPAR;
	if (mode != TRUNC && mode != NOTRUNC)
		return EMODPAR;

	/* find the nul byte of dst */
	for (i = 0; i < dstsize && dst[i] != '\0'; i++)
		;
	if (i == dstsize)       /* no nul byte found into the buffer pointed to by dst */
		return EDSTPAR; /*  dst is a bad-formed string  */
	dstlen = i;

	if (nb == 0)
		return OKNOTRUNC; /* nothing to do */

	remain = dstsize - dstlen;

	for (srclen = 0; srclen < remain && src[srclen] != '\0'; srclen++)
		;
	if (srclen == 0)
		return OKNOTRUNC; /* nothing to do */

	p = dst + dstlen; /* concatenation starting point  */

	/* How many bytes to copy? */
	nb = nb > srclen ? srclen : nb;

	if (remain <= nb) /* dst: not enough space */
	{
		if (mode == TRUNC) /* truncation allowed */
		{
			memcpy(p, src, remain - 1);
			dst[dstsize - 1] = '\0';
			return OKTRUNC;
		} else /*  mode == NOTRUNC */
			return ETRUNC;
	}

	/* dst: enough space */
	memcpy(p, src, nb);
	*(p + nb) = '\0';
	return OKNOTRUNC;
}
