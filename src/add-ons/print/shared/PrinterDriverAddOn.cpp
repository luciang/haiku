/*

PrinterDriverAddOn

Copyright (c) 2003 OpenBeOS. 

Author:
	Michael Pfeiffer
	
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

extern "C" _EXPORT char * add_printer(char * printer_name) {
	return printer_name;
}

extern "C" _EXPORT BMessage * config_page(BNode * spool_dir, BMessage * msg) {
	return NULL;
}

extern "C" _EXPORT BMessage * config_job(BNode * spool_dir, BMessage * msg) {
	return NULL;
}

extern "C" _EXPORT BMessage * default_settings(BNode * printer) {
	return NULL;
}

extern "C" _EXPORT BMessage * take_job(BFile * spool_file, BNode * spool_dir, BMessage * msg) {
	return NULL;
}

