/*
	Copyright 2005, Francois Revol.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/
#ifndef _PEN_INPUT_SERVER_METHOD_H
#define _PEN_INPUT_SERVER_METHOD_H

#include <OS.h>
#include <Messenger.h>
#include <add-ons/input_server/InputServerMethod.h>

#if DEBUG
//#include <File.h>
class BAlert;
#endif
class BList;
class BMessage;
class PenInputLooper;


extern "C" _EXPORT BInputServerMethod* instantiate_input_method();

class PenInputServerMethod : public BInputServerMethod
{
public:
	PenInputServerMethod();
	virtual ~PenInputServerMethod();
	virtual status_t InitCheck();
	virtual filter_result Filter(BMessage *message, BList *outList);
	virtual status_t MethodActivated(bool active);
  
	bool IsEnabled() const { return fEnabled; };
  
private:
	friend PenInputLooper;//DEBUG
	bool fEnabled;
	//BLocker fLocker;
	BMessenger fLooper;
#if DEBUG
	//BFile fDebugFile;
	BAlert *fDebugAlert;
#endif
};

#endif /* _PEN_INPUT_SERVER_METHOD_H */
