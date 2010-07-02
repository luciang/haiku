/* main - the application and startup code
**
** Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
*/


#include "ConfigWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <Locale.h>


class BCatalog;

class MailConfigApp : public BApplication {
	public:
		MailConfigApp();
		~MailConfigApp();
};


MailConfigApp::MailConfigApp()
	: BApplication("application/x-vnd.Haiku-Mail")
{
	(new ConfigWindow())->Show();
}


MailConfigApp::~MailConfigApp()
{
}


//	#pragma mark -


int
main(int argc,char **argv)
{
	(new MailConfigApp())->Run();
	delete be_app;

	return 0;
}

