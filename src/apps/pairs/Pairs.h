/*
 * Copyright 2008, Ralf Schülke, teammaui@web.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PAIRS_H
#define PAIRS_H

#include <Application.h>

extern const char* kSignature;

class BMessage;
class PairsWindow;

class Pairs : public BApplication {
public:
			Pairs();
			virtual ~Pairs();
			
			virtual void ReadyToRun();
			virtual void RefsReceived(BMessage* message);
			virtual void MessageReceived(BMessage* message);

private:
		PairsWindow* fWindow;
};

#endif	// PAIRS_H
