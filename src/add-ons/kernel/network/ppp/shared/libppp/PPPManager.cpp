//-----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003-2004 Waldemar Kornewald, Waldemar.Kornewald@web.de
//-----------------------------------------------------------------------

#include "PPPManager.h"
#include "PPPInterface.h"

#include <cstring>
#include <cstdlib>
#include <cctype>
#include <settings_tools.h>
#include <unistd.h>
#include "_libppputils.h"


PPPManager::PPPManager()
{
	fFD = open(get_stack_driver_path(), O_RDWR);
}


PPPManager::~PPPManager()
{
	if(fFD >= 0)
		close(fFD);
}


status_t
PPPManager::InitCheck() const
{
	if(fFD < 0)
		return B_ERROR;
	else
		return B_OK;
}


status_t
PPPManager::Control(uint32 op, void *data, size_t length) const
{
	if(InitCheck() != B_OK)
		return B_ERROR;
	
	control_net_module_args args;
	args.name = PPP_INTERFACE_MODULE_NAME;
	args.op = op;
	args.data = data;
	args.length = length;
	
	return ioctl(fFD, NET_STACK_CONTROL_NET_MODULE, &args);
}


status_t
PPPManager::ControlModule(const char *name, uint32 op, void *data,
	size_t length) const
{
	if(!name)
		return B_ERROR;
	
	control_net_module_args args;
	args.name = name;
	args.op = op;
	args.data = data;
	args.length = length;
	return Control(PPPC_CONTROL_MODULE, &args, sizeof(args));
}


ppp_interface_id
PPPManager::CreateInterface(const driver_settings *settings,
	const driver_settings *profile = NULL) const
{
	ppp_interface_description_info info;
	info.u.settings = settings;
	info.profile = profile;
	
	if(Control(PPPC_CREATE_INTERFACE, &info, sizeof(info)) != B_OK)
		return PPP_UNDEFINED_INTERFACE_ID;
	else
		return info.interface;
}


ppp_interface_id
PPPManager::CreateInterfaceWithName(const char *name,
	const driver_settings *profile = NULL) const
{
	ppp_interface_description_info info;
	info.u.name = name;
	info.profile = profile;
	
	if(Control(PPPC_CREATE_INTERFACE_WITH_NAME, &info, sizeof(info)) != B_OK)
		return PPP_UNDEFINED_INTERFACE_ID;
	else
		return info.interface;
}


bool
PPPManager::DeleteInterface(ppp_interface_id ID) const
{
	if(Control(PPPC_DELETE_INTERFACE, &ID, sizeof(ID)) != B_OK)
		return false;
	else
		return true;
}


ppp_interface_id*
PPPManager::Interfaces(int32 *count,
	ppp_interface_filter filter = PPP_REGISTERED_INTERFACES) const
{
	int32 requestCount;
	ppp_interface_id *interfaces;
	
	// loop until we get all interfaces
	while(true) {
		requestCount = *count = CountInterfaces(filter);
		if(*count == -1)
			return NULL;
		
		requestCount += 10;
			// request some more interfaces in case some are added in the mean time
		interfaces = new ppp_interface_id[requestCount];
		*count = GetInterfaces(interfaces, requestCount, filter);
		if(*count == -1) {
			delete interfaces;
			return NULL;
		}
		
		if(*count < requestCount)
			break;
		
		delete interfaces;
	}
	
	return interfaces;
}


int32
PPPManager::GetInterfaces(ppp_interface_id *interfaces, int32 count,
	ppp_interface_filter filter = PPP_REGISTERED_INTERFACES) const
{
	ppp_get_interfaces_info info;
	info.interfaces = interfaces;
	info.count = count;
	info.filter = filter;
	
	if(Control(PPPC_GET_INTERFACES, &info, sizeof(info)) != B_OK)
		return -1;
	else
		return info.resultCount;
}


ppp_interface_id
PPPManager::InterfaceWithSettings(const driver_settings *settings) const
{
	ppp_interface_description_info info;
	info.u.settings = settings;
	info.interface = PPP_UNDEFINED_INTERFACE_ID;
	
	Control(PPPC_FIND_INTERFACE_WITH_SETTINGS, &info, sizeof(info));
	
	return info.interface;
}


ppp_interface_id
PPPManager::InterfaceWithUnit(int32 if_unit) const
{
	int32 count;
	ppp_interface_id *interfaces = Interfaces(&count, PPP_REGISTERED_INTERFACES);
	
	if(!interfaces)
		return PPP_UNDEFINED_INTERFACE_ID;
	
	ppp_interface_id id = PPP_UNDEFINED_INTERFACE_ID;
	PPPInterface interface;
	ppp_interface_info_t info;
	
	for(int32 index = 0; index < count; index++) {
		interface.SetTo(interfaces[index]);
		if(interface.InitCheck() == B_OK && interface.GetInterfaceInfo(&info)
				&& info.info.if_unit == if_unit) {
			id = interface.ID();
			break;
		}
	}
	
	delete interfaces;
	
	return id;
}


ppp_interface_id
PPPManager::InterfaceWithName(const char *name) const
{
	if(!name)
		return PPP_UNDEFINED_INTERFACE_ID;
	
	int32 count;
	ppp_interface_id *interfaces = Interfaces(&count, PPP_REGISTERED_INTERFACES);
	
	if(!interfaces)
		return PPP_UNDEFINED_INTERFACE_ID;
	
	ppp_interface_id id = PPP_UNDEFINED_INTERFACE_ID;
	PPPInterface interface;
	ppp_interface_info_t info;
	
	for(int32 index = 0; index < count; index++) {
		interface.SetTo(interfaces[index]);
		if(interface.InitCheck() == B_OK && interface.GetInterfaceInfo(&info)
				&& strlen(info.info.name) > 0 && !strcasecmp(info.info.name, name)) {
			id = interface.ID();
			break;
		}
	}
	
	delete interfaces;
	
	if(id != PPP_UNDEFINED_INTERFACE_ID)
		return id;
	else if(!strncmp(name, "ppp", 3) && strlen(name) > 3 && isdigit(name[3]))
		return InterfaceWithUnit(atoi(name + 3));
	else if(isdigit(name[0]))
		return atoi(name);
	else
		return PPP_UNDEFINED_INTERFACE_ID;
}


int32
PPPManager::CountInterfaces(ppp_interface_filter filter =
	PPP_REGISTERED_INTERFACES) const
{
	return Control(PPPC_COUNT_INTERFACES, &filter, sizeof(filter));
}


bool
PPPManager::EnableReports(ppp_report_type type, thread_id thread,
	int32 flags = PPP_NO_FLAGS) const
{
	ppp_report_request request;
	request.type = type;
	request.thread = thread;
	request.flags = flags;
	
	return Control(PPPC_ENABLE_REPORTS, &request, sizeof(request)) == B_OK;
}


bool
PPPManager::DisableReports(ppp_report_type type, thread_id thread) const
{
	ppp_report_request request;
	request.type = type;
	request.thread = thread;
	
	return Control(PPPC_DISABLE_REPORTS, &request, sizeof(request)) == B_OK;
}
