/***********************************************************
* Base64 library implementation                            *
* @author Ahmed Elzoughby                                  *
* @date July 23, 2017                                      *
***********************************************************/

#include "base64.h"

#ifdef __cplusplus
extern "C" {
#endif

char base46_map[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
					 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
					 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
					 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

char *base64_encode(const char *plain, int len)
{
	char *cipher = malloc(len * 4 / 3 + 4);
	unsigned char counts = 0;
	char buffer[3];
	int i = 0, c = 0;

	for (i = 0; i < len; i++) {
		buffer[counts++] = plain[i];
		if (counts == 3) {
			cipher[c++] = base46_map[buffer[0] >> 2];
			cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
			cipher[c++] = base46_map[((buffer[1] & 0x0f) << 2) + (buffer[2] >> 6)];
			cipher[c++] = base46_map[buffer[2] & 0x3f];
			counts = 0;
		}
	}

	if (counts > 0) {
		cipher[c++] = base46_map[buffer[0] >> 2];
		if (counts == 1) {
			cipher[c++] = base46_map[(buffer[0] & 0x03) << 4];
			cipher[c++] = '=';
		} else {
			// if counts == 2
			cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
			cipher[c++] = base46_map[(buffer[1] & 0x0f) << 2];
		}
		cipher[c++] = '=';
	}

	/* string padding character */
	cipher[c] = '\0';
	return cipher;
}

char *base64_decode(const char *cipher, int len)
{
	unsigned char counts = 0;
	int i = 0, p = 0;
	char buffer[4];
	char *plain;

	if (len < 1)
		return NULL;
	plain = malloc(len);
	if (!plain)
		return NULL;

	for (i = 0; i < len; i++) {
		unsigned char k;

		for (k = 0; k < 64 && base46_map[k] != cipher[i]; k++);
		buffer[counts++] = k;
		if (counts == 4) {
			plain[p++] = (buffer[0] << 2) + (buffer[1] >> 4);
			if (buffer[2] != 64)
				plain[p++] = (buffer[1] << 4) + (buffer[2] >> 2);
			if (buffer[3] != 64)
				plain[p++] = (buffer[2] << 6) + buffer[3];
			counts = 0;
		}
	}
	plain[p] = '\0';    /* string padding character */

	return plain;
}

#ifdef __cplusplus
}
#endif
