    /***********************************************************
    * Base64 library                                           *
    * @author Ahmed Elzoughby                                  *
    * @date July 23, 2017                                      *
    * Purpose: encode and decode base64 format                 *
    ***********************************************************/

#ifndef _MY_BASE46_H
#define _MY_BASE46_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************
Encodes ASCCI string into base64 format string
@param plain ASCII buffer to be encoded
@param length of the buffer
@return encoded base64 format string
***********************************************/
char *base64_encode(const char *encoded, int len);


/***********************************************
decodes base64 format string into ASCCI string
@param plain encoded base64 format buffer
@param length of the buffer
@return ASCII string to be encoded
***********************************************/
char *base64_decode(const char *encoded, int len);

#ifdef __cplusplus
}
#endif

#endif //_MY_BASE46_H
