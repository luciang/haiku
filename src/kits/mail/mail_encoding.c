#include <ctype.h>
#include <string.h>

#include <mail_encoding.h>

#define	DEC(Char) (((Char) - ' ') & 077)

typedef unsigned char uchar;

char base64_alphabet[64] = { //----Fast lookup table
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  '+',
  '/'
 };

const char hex_alphabet[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};


_EXPORT ssize_t
encode(mail_encoding encoding, char *out, const char *in, off_t length, int headerMode)
{
	switch (encoding) {
		case base64:
			return encode_base64(out,in,length,headerMode);
		case quoted_printable:
			return encode_qp(out,in,length,headerMode);
		case seven_bit:
		case eight_bit:
		case no_encoding:
			memcpy(out,in,length);
			return length;
		case uuencode:
		default:
			return -1;
	}
	
	return -1;
}
			

_EXPORT ssize_t
decode(mail_encoding encoding, char *out, const char *in, off_t length, int underscore_is_space)
{
	switch (encoding) {
		case base64:
			return decode_base64(out, in, length);
		case uuencode:
			return uu_decode(out, in, length);
		case seven_bit:
		case eight_bit:
		case no_encoding:
			memcpy(out, in, length);
			return length;
		case quoted_printable:
			return decode_qp(out, in, length, underscore_is_space);
		default:
			break;
	}

	return -1;
}


_EXPORT ssize_t
max_encoded_length(mail_encoding encoding, off_t cur_length)
{
	switch (encoding) {
		case base64:
		{
			double result;
			result = cur_length;
			result *= 1.33333333333333;
			result += (result / BASE64_LINELENGTH)*2 + 20;
			return (ssize_t)(result);
		}
		case quoted_printable:
			return cur_length*3;
		case seven_bit:
		case eight_bit:
		case no_encoding:
			return cur_length;
		case uuencode:
		default:
			return -1;
	}
	
	return -1;
}


_EXPORT mail_encoding
encoding_for_cte(const char *cte)
{
	if (cte == NULL)
		return no_encoding;
	
	if (strcasecmp(cte,"uuencode") == 0)
		return uuencode;
	if (strcasecmp(cte,"base64") == 0)
		return base64;
	if (strcasecmp(cte,"quoted-printable") == 0)
		return quoted_printable;
	if (strcasecmp(cte,"7bit") == 0)
		return seven_bit;
	if (strcasecmp(cte,"8bit") == 0)
		return eight_bit;

	return no_encoding;
}


_EXPORT ssize_t
encode_base64(char *out, const char *in, off_t length, int headerMode)
{
	unsigned long concat;
	int i = 0;
	int k = 0;
	int curr_linelength = 4; //--4 is a safety extension, designed to cause retirement *before* it actually gets too long

	while (i < length) {
		concat = ((in[i] & 0xff) << 16);
		
		if ((i+1) < length)
			concat |= ((in[i+1] & 0xff) << 8);
		if ((i+2) < length)
			concat |= (in[i+2] & 0xff);
			
		i += 3;
				
		out[k++] = base64_alphabet[(concat >> 18) & 63];
		out[k++] = base64_alphabet[(concat >> 12) & 63];
		out[k++] = base64_alphabet[(concat >> 6) & 63];
		out[k++] = base64_alphabet[concat & 63];

		if (i >= length) {
			int v;
			for (v = 0; v <= (i - length); v++)
				out[k-v] = '=';
		}

		curr_linelength += 4;
		
		// No line breaks in header mode, since the text is part of a Subject:
		// line or some other single header line.  The header code will do word
		// wrapping separately from this encoding stuff.
		if (!headerMode && curr_linelength > BASE64_LINELENGTH) {
			out[k++] = '\r';
			out[k++] = '\n';
			
			curr_linelength = 4;
		}
	}
	
	return k;
}


_EXPORT ssize_t
decode_base64(char *out, const char *in, off_t length)
{
	unsigned long concat, value;
	int lastOutLine = 0;
	int i, j;
	int outIndex = 0;

	for (i = 0; i < length; i += 4) {
		concat = 0;

		for (j = 0; j < 4 && (i + j) < length; j++) {
			value = in[i + j];

			if (value == '\n' || value == '\r') {
				// jump over line breaks
				lastOutLine = outIndex;
				i++;
				j--;
				continue;
			}

			if ((value >= 'A') && (value <= 'Z'))
				value -= 'A';
			else if ((value >= 'a') && (value <= 'z'))
				value = value - 'a' + 26;
			else if ((value >= '0') && (value <= '9'))
				value = value - '0' + 52;
			else if (value == '+')
				value = 62;
			else if (value == '/')
				value = 63;
			else if (value == '=')
				break;
			else {
				// there is an invalid character in this line - we will
				// ignore the whole line and go to the next
				outIndex = lastOutLine;
				while (i < length && in[i] != '\n' && in[i] != '\r')
					i++;
				concat = 0;
			}

			value = value << ((3-j)*6);

			concat |= value;
		}

		if (j > 1)
			out[outIndex++] = (concat & 0x00ff0000) >> 16;
		if (j > 2)
			out[outIndex++] = (concat & 0x0000ff00) >> 8;
		if (j > 3)
			out[outIndex++] = (concat & 0x000000ff);
	}

	return outIndex;
}


_EXPORT ssize_t
decode_qp(char *out, const char *in, off_t length, int underscore_is_space)
{
	// decode Quoted Printable
	char *dataout = out;
	const char *datain = in, *dataend = in+length;
	
	while ( datain < dataend )
	{
		if (*datain == '=' && dataend-datain>2)
		{
			int a,b;
			
			a = toupper(datain[1]);
			a -= a>='0' && a<='9'? '0' : (a>='A' && a<='F'? 'A'-10 : a+1);
			
			b = toupper(datain[2]);
			b -= b>='0' && b<='9'? '0' : (b>='A' && b<='F'? 'A'-10 : b+1);
			
			if (a>=0 && b>=0)
			{
				*dataout++ = (a<<4) + b;
				datain += 3;
				continue;
			} else if (datain[1]=='\r' && datain[2]=='\n') {
				// strip =<CR><NL>
				datain += 3;
				continue;
			}
		}
		else if ((*datain == '_') && (underscore_is_space))
		{
			*dataout++ = ' ';
			++datain;
			continue;
		}
		
		*dataout++ = *datain++;
	}
	
	*dataout = '\0';
	return dataout-out;	
}


_EXPORT ssize_t
encode_qp(char *out, const char *in, off_t length, int headerMode)
{
	int g = 0, i = 0;
	
	for (; i < length; i++) {
		if ((((unsigned char *)(in))[i] > 127) ||
			(in[i] == '?') ||
			(in[i] == '=') ||
			(in[i] == '_') ||
			// Also encode the letter F in "From " at the start of the line,
			// which Unix systems use to mark the start of messages in their
			// mbox files.
			(in[i] == 'F' &&
				(i + 5 <= length) &&
				(i == 0 || in[i-1] == '\n') &&
				in[i+1] == 'r' &&
				in[i+2] == 'o' &&
				in[i+3] == 'm' &&
				in[i+4] == ' ')) {
			out[g++] = '=';
			out[g++] = hex_alphabet[(in[i] >> 4) & 0x0f];
			out[g++] = hex_alphabet[in[i] & 0x0f];
		}
		else if (headerMode && (in[i] == ' ' || in[i] == '\t'))
			out[g++] = '_';
		else if (headerMode && (in[i] >= 0 && in[i] < 32)) {
			// Control codes in headers need to be sanitized, otherwise certain
			// Japanese ISPs mangle the headers badly.  But they don't mangle
			// the body.
			out[g++] = '=';
			out[g++] = hex_alphabet[(in[i] >> 4) & 0x0f];
			out[g++] = hex_alphabet[in[i] & 0x0f];
		} else
			out[g++] = in[i];
	}

	return g;
}


_EXPORT ssize_t
uu_decode(char *out, const char *in, off_t length)
{
	long n;
	uchar *p,*inBuffer = (uchar *)in;
	uchar *outBuffer = (uchar *)out;
	
	inBuffer = (uchar *)strstr((char *)inBuffer, "begin");
	goto enterLoop;

	while (((inBuffer - (uchar *)in) <= length) && strncmp((char *)inBuffer, "end", 3)) {
		p = inBuffer;
		n = DEC(inBuffer[0]);

		for (++inBuffer; n > 0; inBuffer += 4, n -= 3) {
			if (n >= 3) {
				*outBuffer++ = DEC(inBuffer[0]) << 2 | DEC (inBuffer[1]) >> 4;
				*outBuffer++ = DEC(inBuffer[1]) << 4 | DEC (inBuffer[2]) >> 2;
				*outBuffer++ = DEC(inBuffer[2]) << 6 | DEC (inBuffer[3]);
			} else {
				if (n >= 1) *outBuffer++ = DEC(inBuffer[0]) << 2
					| DEC (inBuffer[1]) >> 4;
				if (n >= 2) *outBuffer++ = DEC(inBuffer[1]) << 4
					| DEC (inBuffer[2]) >> 2;
			}
		}
		inBuffer = p;

	enterLoop:
		while ((inBuffer[0] != '\n') && (inBuffer[0] != '\r')
			&& (inBuffer[0] != 0)) inBuffer++;
		while ((inBuffer[0] == '\n') || (inBuffer[0] == '\r')) inBuffer++;
	}

	return (ssize_t)(outBuffer - ((uchar *)in));
}

