//-----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003-2004 Waldemar Kornewald, Waldemar.Kornewald@web.de
//-----------------------------------------------------------------------

#include <KPPPOptionHandler.h>

#include <PPPControl.h>


KPPPOptionHandler::KPPPOptionHandler(const char *name, uint8 type,
		KPPPInterface& interface, driver_parameter *settings)
	: fInitStatus(B_OK),
	fType(type),
	fInterface(interface),
	fSettings(settings),
	fEnabled(true)
{
	if(name)
		fName = strdup(name);
	else
		fName = NULL;
}


KPPPOptionHandler::~KPPPOptionHandler()
{
	free(fName);
	
	Interface().LCP().RemoveOptionHandler(this);
}


status_t
KPPPOptionHandler::InitCheck() const
{
	return fInitStatus;
}


status_t
KPPPOptionHandler::Control(uint32 op, void *data, size_t length)
{
	switch(op) {
		case PPPC_GET_SIMPLE_HANDLER_INFO: {
			if(length < sizeof(ppp_simple_handler_info_t) || !data)
				return B_ERROR;
			
			ppp_simple_handler_info *info = (ppp_simple_handler_info*) data;
			memset(info, 0, sizeof(ppp_simple_handler_info_t));
			if(Name())
				strncpy(info->name, Name(), PPP_HANDLER_NAME_LENGTH_LIMIT);
			info->isEnabled = IsEnabled();
		} break;
		
		case PPPC_ENABLE:
			if(length < sizeof(uint32) || !data)
				return B_ERROR;
			
			SetEnabled(*((uint32*)data));
		break;
		
		default:
			return B_BAD_VALUE;
	}
	
	return B_OK;
}


status_t
KPPPOptionHandler::StackControl(uint32 op, void *data)
{
	switch(op) {
		default:
			return B_BAD_VALUE;
	}
	
	return B_OK;
}


void
KPPPOptionHandler::ProfileChanged()
{
	// do nothing by default
}
