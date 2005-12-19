/*
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <SupportDefs.h>
#include <string.h>
#include <util/kernel_cpp.h>

#include "Handle.h"
#include "console.h"
#include "openfirmware.h"


class ConsoleHandle : public Handle {
	public:
		ConsoleHandle();

		virtual ssize_t ReadAt(void *cookie, off_t pos, void *buffer,
			size_t bufferSize);
		virtual ssize_t WriteAt(void *cookie, off_t pos, const void *buffer,
			size_t bufferSize);
};

class InputConsoleHandle : public ConsoleHandle {
	public:
		InputConsoleHandle();

		virtual ssize_t ReadAt(void *cookie, off_t pos, void *buffer,
			size_t bufferSize);

		void PutChar(char c);
		void PutChars(const char *buffer, int count);
		char GetChar();

	private:
		enum { BUFFER_SIZE = 32 };

		char	fBuffer[BUFFER_SIZE];
		int		fStart;
		int		fCount;
};


static InputConsoleHandle sInput;
static ConsoleHandle sOutput;
FILE *stdin, *stdout, *stderr;
static bool sCursorVisible = true;


ConsoleHandle::ConsoleHandle()
	: Handle()
{
}


ssize_t
ConsoleHandle::ReadAt(void */*cookie*/, off_t /*pos*/, void *buffer,
	size_t bufferSize)
{
	// don't seek in character devices
	return of_read(fHandle, buffer, bufferSize);
}


ssize_t
ConsoleHandle::WriteAt(void */*cookie*/, off_t /*pos*/, const void *buffer,
	size_t bufferSize)
{
	const char *string = (const char *)buffer;

	// be nice to our audience and replace single "\n" with "\r\n"

	while (bufferSize > 0) {
		bool newLine = false;
		size_t length = 0;

		for (; length < bufferSize; length++) {
			if (string[length] == '\r' && length + 1 < bufferSize) {
				length += 2;
			} else if (string[length] == '\n') {
				newLine = true;
				break;
			}
		}

		if (length > 0) {
			of_write(fHandle, string, length);
			string += length;
			bufferSize -= length;
		}

		if (newLine) {
			// this code replaces a single '\n' with '\r\n', so it
			// bumps the string/bufferSize only a single character
			of_write(fHandle, "\r\n", 2);
			string++;
			bufferSize--;
		}
	}

	return string - (char *)buffer;
}


//	#pragma mark -


InputConsoleHandle::InputConsoleHandle()
	: ConsoleHandle()
	, fStart(0)
	, fCount(0)
{
}


ssize_t
InputConsoleHandle::ReadAt(void */*cookie*/, off_t /*pos*/, void *_buffer,
	size_t bufferSize)
{
	char *buffer = (char*)_buffer;

	// copy buffered bytes first
	int bytesTotal = 0;
	while (bufferSize > 0 && fCount > 0) {
		*buffer++ = GetChar();
		bytesTotal++;
		bufferSize--;
	}

	// read from console
	if (bufferSize > 0) {
		ssize_t bytesRead = ConsoleHandle::ReadAt(NULL, 0, buffer, bufferSize);
		if (bytesRead < 0)
			return bytesRead;
		bytesTotal += bytesRead;
	}

	return bytesTotal;
}


void
InputConsoleHandle::PutChar(char c)
{
	if (fCount >= BUFFER_SIZE)
		return;

	int pos = (fStart + fCount) % BUFFER_SIZE;
	fBuffer[pos] = c;
	fCount++;
}


void
InputConsoleHandle::PutChars(const char *buffer, int count)
{
	for (int i = 0; i < count; i++)
		PutChar(buffer[i]);
}

		
char
InputConsoleHandle::GetChar()
{
	if (fCount == 0)
		return 0;

	fCount--;
	char c = fBuffer[fStart];
	fStart = (fStart + 1) % BUFFER_SIZE;
	return c;
}


//	#pragma mark -


status_t
console_init(void)
{
	int input, output;
	if (of_getprop(gChosen, "stdin", &input, sizeof(int)) == OF_FAILED)
		return B_ERROR;
	if (of_getprop(gChosen, "stdout", &output, sizeof(int)) == OF_FAILED)
		return B_ERROR;

	sInput.SetHandle(input);
	sOutput.SetHandle(output);

	// now that we're initialized, enable stdio functionality
	stdin = (FILE *)&sInput;
	stdout = stderr = (FILE *)&sOutput;

	return B_OK;
}


// #pragma mark -


void
console_clear_screen(void)
{
	of_interpret("erase-screen", 0, 0);
}


int32
console_width(void)
{
	int columnCount;
	if (of_interpret("#columns", 0, 1, &columnCount) == OF_FAILED)
		return 0;
	return columnCount;
}


int32
console_height(void)
{
	int lineCount;
	if (of_interpret("#lines", 0, 1, &lineCount) == OF_FAILED)
		return 0;
	return lineCount;
}


void
console_set_cursor(int32 x, int32 y)
{
	// Note: We toggle the cursor temporarily to prevent a cursor artifact at
	// at the old location.
	of_interpret("toggle-cursor"
		" to line#"
		" to column#"
		" toggle-cursor",
		2, 0, y, x);

}


static int
translate_color(int32 color)
{
	/*
			r	g	b
		0:	0	0	0		// black
		1:	0	0	aa		// dark blue
		2:	0	aa	0		// dark green
		3:	0	aa	aa		// cyan
		4:	aa	0	0		// dark red
		5:	aa	0	aa		// purple
		6:	aa	55	0		// brown
		7:	aa	aa	aa		// light gray
		8:	55	55	55		// dark gray
		9:	55	55	ff		// light blue
		a:	55	ff	55		// light green
		b:	55	ff	ff		// light cyan
		c:	ff	55	55		// light red
		d:	ff	55	ff		// magenta
		e:	ff	ff	55		// yellow
		f:	ff	ff	ff		// white
	*/
	if (color >= 0 && color < 16)
		return color;
	return 0;
}


void
console_set_color(int32 foreground, int32 background)
{
	// Note: Toggling the cursor doesn't seem to help. We still get cursor
	// artifacts.
	of_interpret("toggle-cursor"
		" to foreground-color"
		" to background-color"
		" toggle-cursor",
		2, 0, translate_color(foreground), translate_color(background));
}


int
console_wait_for_key(void)
{
	// wait for a key
	char buffer[3];
	ssize_t bytesRead;
	do {
		bytesRead = sInput.ReadAt(NULL, 0, buffer, 3);
		if (bytesRead < 0)
			return 0;
	} while (bytesRead == 0);

	// translate the ESC sequences for cursor keys
	if (bytesRead == 3 && buffer[0] == 27 && buffer [1] == 91) {
		switch (buffer[2]) {
			case 65:
				return TEXT_CONSOLE_KEY_UP;
			case 66:
				return TEXT_CONSOLE_KEY_DOWN;
			case 67:
				return TEXT_CONSOLE_KEY_RIGHT;
			case 68:
				return TEXT_CONSOLE_KEY_LEFT;
// TODO: Translate the codes for the following keys. Unfortunately my OF just
// returns a '\0' character. :-/
// 			TEXT_CONSOLE_KEY_PAGE_UP,
// 			TEXT_CONSOLE_KEY_PAGE_DOWN,
// 			TEXT_CONSOLE_KEY_HOME,
// 			TEXT_CONSOLE_KEY_END,

			default:
				break;
		}
	}

	// put back unread chars
	if (bytesRead > 1)
		sInput.PutChars(buffer + 1, bytesRead - 1);

	return buffer[0];
}

