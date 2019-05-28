/*	@(#)	str5.h	*/
/* 
 *	v 2.0, 2016/05, Eric Sanchis <eric.sanchis@iut-rodez.fr>
 *	GNU LGPL
 */


#ifndef _STR5_H

#define	_STR5_H

#include	<string.h>


#define   TRUNC      0		/* truncation allowed */
#define   NOTRUNC    1		/* truncation not allowed */

#define   OKNOTRUNC  0		/* copy/concatenation performed without truncation */
#define   OKTRUNC    1		/* copy/concatenation performed with truncation */



#define   EDSTPAR   -1		/* Error : bad dst parameters */
#define   ESRCPAR   -2		/* Error : bad src parameters */
#define   EMODPAR   -3		/* Error : bad mode parameter */
#define   ETRUNC    -4		/* Error : not enough space to copy/concatenate
							   and truncation not allowed */



int str5cpy( char * dst,
             size_t dstsize,
             const char * src,
             size_t nb,
             size_t mode ) ;

int str5cat( char * dst,
             size_t dstsize,
             const char * src,
             size_t nb,
             size_t mode ) ;

#define	strtcpy(dst,dstsize,src)	str5cpy(dst,dstsize,src,dstsize,TRUNC)

#define	strtcat(dst,dstsize,src)	str5cat(dst,dstsize,src,dstsize,TRUNC)

#endif
