/*
 * Copyright 2008, Michael Pfeiffer, laplace@users.sourceforge.net. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "WizardController.h"

#include "WizardView.h"
#include "WizardPageView.h"

void
WizardController::StateStack::MakeEmpty()
{
		StateStack* stack = this;
		StateStack* next;
		do {
			next = stack->Next();
			delete stack;
			stack = next;
		} while (next != NULL);						
}


WizardController::WizardController()
	: fStack(NULL)
{
}


WizardController::~WizardController()
{
	if (fStack != NULL) {
		fStack->MakeEmpty();
		fStack = NULL;
	}
}


void
WizardController::Initialize(WizardView* view)
{
	if (fStack == NULL)
		_PushState(InitialState());
	_ShowPage(view);
}


void
WizardController::Next(WizardView* wizard)
{
	wizard->PageCompleted();
	
	if (fStack == NULL)
		return;
	
	int state = NextState(fStack->State());
	if (state < 0)
		return;
				
	_PushState(state);
	_ShowPage(wizard);
}


void
WizardController::Previous(WizardView* wizard)
{
	wizard->PageCompleted();

	if (fStack != NULL) {
		StateStack* stack = fStack;
		fStack = fStack->Next();
		delete stack;
	}
	_ShowPage(wizard);
}


void
WizardController::_PushState(int32 state)
{
	fStack = new StateStack(state, fStack);
}


void
WizardController::_ShowPage(WizardView* wizard)
{
	if (fStack == NULL)
		return;

	WizardPageView* page = CreatePage(fStack->State(), wizard);
	wizard->SetPage(page);
}
