#include <mail_encoding.h>

#if __MWERKS__
	#define encode_base64__local_abi encode_base64__FPcPcx
	#define decode_base64__local_abi decode_base64__FPcPcxb
	#define USETHISFILEATALL=1
#elif __GNUC__ <= 2
	#define encode_base64__local_abi encode_base64__FPcT0x
	#define decode_base64__local_abi decode_base64__FPcT0xb
	#define USETHISFILEATALL=1	
#endif

#if USETHISFILEATALL /* If we are using GCC >= 3 or something else, we clearly have given up on binary compat anyway */

ssize_t encode_base64__local_abi(char *out, char *in, off_t length);
ssize_t decode_base64__local_abi(char *out, char *in, off_t length, char);

_EXPORT ssize_t encode_base64__local_abi(char *out, char *in, off_t length) {
	return encode_base64(out,in,length,0 /* headerMode */);
}

_EXPORT ssize_t decode_base64__local_abi(char *out, char *in, off_t length, char nothing) {
	nothing = '\0';
	return decode_base64(out,in,length);
}

#endif

