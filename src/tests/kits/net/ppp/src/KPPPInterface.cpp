//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Waldemar Kornewald, Waldemar.Kornewald@web.de
//---------------------------------------------------------------------

// stdio.h must be included before PPPModule.h/PPPManager.h because
// dprintf is defined twice with different return values, once with
// void (KernelExport.h) and once with int (stdio.h).
#include <cstdio>
#include <cstring>
#include <new.h>

// now our headers...
#include <KPPPInterface.h>

// our other classes
#include <PPPControl.h>
#include <KPPPDevice.h>
#include <KPPPEncapsulator.h>
#include <KPPPLCPExtension.h>
#include <KPPPOptionHandler.h>
#include <KPPPModule.h>
#include <KPPPManager.h>
#include <KPPPUtils.h>

// general helper classes not only belonging to us
#include <LockerHelper.h>

// tools only for us :)
#include "settings_tools.h"

// internal modules
#include "_KPPPPFCHandler.h"


// TODO:
// - implement timers with support for settings next time instead of receiving timer
//    events periodically


// needed for redial:
typedef struct redial_info {
	PPPInterface *interface;
	thread_id *thread;
	uint32 delay;
} redial_info;

status_t redial_thread(void *data);

// other functions
status_t in_queue_thread(void *data);
status_t interface_deleter_thread(void *data);


PPPInterface::PPPInterface(uint32 ID, driver_settings *settings,
		PPPInterface *parent = NULL)
	: fID(ID),
	fSettings(dup_driver_settings(settings)),
	fStateMachine(*this),
	fLCP(*this),
	fReportManager(StateMachine().Locker()),
	fIfnet(NULL),
	fUpThread(-1),
	fRedialThread(-1),
	fDialRetry(0),
	fDialRetriesLimit(0),
	fIdleSince(0),
	fMRU(1500),
	fInterfaceMTU(1498),
	fHeaderLength(2),
	fAutoRedial(false),
	fDialOnDemand(false),
	fLocalPFCState(PPP_PFC_DISABLED),
	fPeerPFCState(PPP_PFC_DISABLED),
	fPFCOptions(0),
	fDevice(NULL),
	fFirstEncapsulator(NULL),
	fLock(StateMachine().Locker()),
	fDeleteCounter(0)
{
	// add internal modules
	_PPPPFCHandler *pfcHandler =
		new _PPPPFCHandler(fLocalPFCState, fPeerPFCState, *this);
	if(pfcHandler->InitCheck() != B_OK)
		delete pfcHandler;
	
	// set up dial delays
	fDialRetryDelay = 3000;
		// 3s delay between each new attempt to redial
	fRedialDelay = 1000;
		// 1s delay between lost connection and redial
	
	// set up queue
	fInQueue = start_ifq();
	fInQueueThread = spawn_thread(in_queue_thread, "PPPInterface: in_queue_thread",
		B_NORMAL_PRIORITY, this);
	resume_thread(fInQueueThread);
	
	if(get_module(PPP_MANAGER_MODULE_NAME, (module_info**) &fManager) != B_OK)
		fManager = NULL;
	
	// are we a multilink subinterface?
	if(parent && parent->IsMultilink()) {
		fParent = parent;
		fParent->AddChild(this);
		fIsMultilink = true;
	} else {
		fParent = NULL;
		fIsMultilink = false;
	}
	
	if(!fSettings) {
		fMode = PPP_CLIENT_MODE;
		fInitStatus = B_ERROR;
		return;
	}
	
	const char *value;
	
	// get DisonnectAfterIdleSince settings
	value = get_settings_value(PPP_DISONNECT_AFTER_IDLE_SINCE_KEY, fSettings);
	if(!value)
		fDisconnectAfterIdleSince = 0;
	else
		fDisconnectAfterIdleSince = atoi(value) * 1000;
	
	if(fDisconnectAfterIdleSince < 0)
		fDisconnectAfterIdleSince = 0;
	
	// get mode settings
	value = get_settings_value(PPP_MODE_KEY, fSettings);
	if(!strcasecmp(value, PPP_SERVER_MODE_VALUE))
		fMode = PPP_SERVER_MODE;
	else
		fMode = PPP_CLIENT_MODE;
		// we are a client by default
	
	SetDialOnDemand(
		get_boolean_value(
		get_settings_value(PPP_DIAL_ON_DEMAND_KEY, fSettings),
		false)
		);
		// dial on demand is disabled by default
	
	
	SetAutoRedial(
		get_boolean_value(
		get_settings_value(PPP_AUTO_REDIAL_KEY, fSettings),
		false)
		);
		// auto redial is disabled by default
	
	// load all protocols and the device
	if(LoadModules(fSettings, 0, fSettings->parameter_count))
		fInitStatus = B_OK;
	else
		fInitStatus = B_ERROR;
}


PPPInterface::~PPPInterface()
{
	++fDeleteCounter;
	
	// make sure we are not accessible by any thread before we continue
	UnregisterInterface();
	
	if(fManager)
		fManager->remove_interface(ID());
	
	// Call Down() until we get a lock on an interface that is down.
	// This lock is not released until we are actually deleted.
	while(true) {
		Down();
		fLock.Lock();
		if(State() == PPP_INITIAL_STATE && Phase() == PPP_DOWN_PHASE)
			break;
		fLock.Unlock();
	}
	
	Report(PPP_DESTRUCTION_REPORT, 0, NULL, 0);
		// tell all listeners that we are being destroyed
	
	int32 tmp;
	stop_ifq(InQueue());
	wait_for_thread(fInQueueThread, &tmp);
	
	send_data_with_timeout(fRedialThread, 0, NULL, 0, 200);
		// tell thread that we are being destroyed (200ms timeout)
	wait_for_thread(fRedialThread, &tmp);
	
	while(CountChildren())
		delete ChildAt(0);
	
	delete Device();
	
	while(CountProtocols())
		delete ProtocolAt(0);
	
	while(FirstEncapsulator())
		delete FirstEncapsulator();
	
	for(int32 index = 0; index < fModules.CountItems(); index++) {
		put_module(fModules.ItemAt(index));
		free(fModules.ItemAt(index));
	}
	
	free_driver_settings(fSettings);
	
	if(Parent())
		Parent()->RemoveChild(this);
	
	if(fManager)
		put_module(PPP_MANAGER_MODULE_NAME);
}


void
PPPInterface::Delete()
{
	if(atomic_add(&fDeleteCounter, 1) > 0)
		return;
			// only one thread should delete us!
	
	if(fManager)
		fManager->delete_interface(ID());
			// This will mark us for deletion.
			// Any subsequent calls to delete_interface() will do nothing.
	else {
		// We were not created by the manager.
		// Spawn a thread that will delete us.
		thread_id interfaceDeleterThread = spawn_thread(interface_deleter_thread,
			"PPPInterface: interface_deleter_thread", B_NORMAL_PRIORITY, this);
		resume_thread(interfaceDeleterThread);
	}
}


status_t
PPPInterface::InitCheck() const
{
	if(!fSettings || !fManager || fInitStatus != B_OK)
		return B_ERROR;
	
	// sub-interfaces should have a device
	if(IsMultilink()) {
		if(Parent() && !fDevice)
			return B_ERROR;
	} else if(!fDevice)
		return B_ERROR;
	
	return B_OK;
}


void
PPPInterface::SetMRU(uint32 MRU)
{
	LockerHelper locker(fLock);
	
	fMRU = MRU;
	
	CalculateInterfaceMTU();
}


status_t
PPPInterface::Control(uint32 op, void *data, size_t length)
{
	switch(op) {
		case PPPC_GET_INTERFACE_INFO: {
			if(length < sizeof(ppp_interface_info_t) || !data)
				return B_NO_MEMORY;
			
			ppp_interface_info *info = (ppp_interface_info*) data;
			memset(info, 0, sizeof(ppp_interface_info_t));
			info->settings = Settings();
			info->mode = Mode();
			info->state = State();
			info->phase = Phase();
			info->localAuthenticationStatus =
				StateMachine().LocalAuthenticationStatus();
			info->peerAuthenticationStatus =
				StateMachine().PeerAuthenticationStatus();
			info->localPFCState = LocalPFCState();
			info->peerPFCState = PeerPFCState();
			info->pfcOptions = PFCOptions();
			info->protocolsCount = CountProtocols();
			info->encapsulatorsCount = CountEncapsulators();
			info->optionHandlersCount = LCP().CountOptionHandlers();
			info->LCPExtensionsCount = 0;
			info->childrenCount = CountChildren();
			info->MRU = MRU();
			info->interfaceMTU = InterfaceMTU();
			info->dialRetry = fDialRetry;
			info->dialRetriesLimit = fDialRetriesLimit;
			info->dialRetryDelay = DialRetryDelay();
			info->redialDelay = RedialDelay();
			info->idleSince = IdleSince();
			info->disconnectAfterIdleSince = DisconnectAfterIdleSince();
			info->doesDialOnDemand = DoesDialOnDemand();
			info->doesAutoRedial = DoesAutoRedial();
			info->hasDevice = Device();
			info->isMultilink = IsMultilink();
			info->hasParent = Parent();
		} break;
		
		case PPPC_SET_MRU:
			if(length < sizeof(uint32) || !data)
				return B_ERROR;
			
			SetMRU(*((uint32*)data));
		break;
		
		case PPPC_SET_DIAL_ON_DEMAND:
			if(length < sizeof(uint32) || !data)
				return B_NO_MEMORY;
			
			SetDialOnDemand(*((uint32*)data));
		break;
		
		case PPPC_SET_AUTO_REDIAL:
			if(length < sizeof(uint32) || !data)
				return B_NO_MEMORY;
			
			SetAutoRedial(*((uint32*)data));
		break;
		
		case PPPC_ENABLE_INTERFACE_REPORTS: {
			if(length < sizeof(ppp_report_request) || !data)
				return B_ERROR;
			
			ppp_report_request *request = (ppp_report_request*) data;
			ReportManager().EnableReports(request->type, request->thread,
				request->flags);
		} break;
		
		case PPPC_DISABLE_INTERFACE_REPORTS: {
			if(length < sizeof(ppp_report_request) || !data)
				return B_ERROR;
			
			ppp_report_request *request = (ppp_report_request*) data;
			ReportManager().DisableReports(request->type, request->thread);
		} break;
		
		case PPPC_CONTROL_DEVICE: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			if(control->index != 0 || !Device())
				return B_BAD_INDEX;
			
			return Device()->Control(control->op, control->data, control->length);
		} break;
		
		case PPPC_CONTROL_PROTOCOL: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			PPPProtocol *protocol_handler = ProtocolAt(control->index);
			if(!protocol_handler)
				return B_BAD_INDEX;
			
			return protocol_handler->Control(control->op, control->data,
				control->length);
		} break;
		
		case PPPC_CONTROL_ENCAPSULATOR: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			PPPEncapsulator *encapsulator_handler = EncapsulatorAt(control->index);
			if(!encapsulator_handler)
				return B_BAD_INDEX;
			
			return encapsulator_handler->Control(control->op, control->data,
				control->length);
		} break;
		
		case PPPC_CONTROL_OPTION_HANDLER: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			PPPOptionHandler *option_handler = LCP().OptionHandlerAt(control->index);
			if(!option_handler)
				return B_BAD_INDEX;
			
			return option_handler->Control(control->op, control->data,
				control->length);
		} break;
		
		case PPPC_CONTROL_LCP_EXTENSION: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			PPPLCPExtension *extension_handler = LCP().LCPExtensionAt(control->index);
			if(!extension_handler)
				return B_BAD_INDEX;
			
			return extension_handler->Control(control->op, control->data,
				control->length);
		} break;
		
		case PPPC_CONTROL_CHILD: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			PPPInterface *child = ChildAt(control->index);
			if(!child)
				return B_BAD_INDEX;
			
			return child->Control(control->op, control->data, control->length);
		} break;
		
		case PPPC_STACK_IOCTL: {
			if(length < sizeof(ppp_control_info) || !data)
				return B_ERROR;
			
			ppp_control_info *control = (ppp_control_info*) data;
			if(StackControl(control->op, control->data) == B_BAD_VALUE)
				return ControlEachHandler(op, data, length);
			
			return B_OK;
		} break;
		
		default:
			return B_BAD_VALUE;
	}
	
	return B_OK;
}


bool
PPPInterface::SetDevice(PPPDevice *device)
{
	if(!device)
		return false;
	
	if(IsMultilink() && !Parent())
		return false;
			// main interfaces do not have devices
	
	LockerHelper locker(fLock);
	
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	if(fDevice && (IsUp() || fDevice->IsUp()))
		Down();
	
	fDevice = device;
	
	fMRU = fDevice->MTU();
	
	CalculateInterfaceMTU();
	CalculateBaudRate();
	
	return true;
}


bool
PPPInterface::AddProtocol(PPPProtocol *protocol)
{
	if(!protocol)
		return false;
	
	LockerHelper locker(fLock);
	
	if(Phase() != PPP_DOWN_PHASE
			|| (protocol->AuthenticatorType() != PPP_NO_AUTHENTICATOR
				&& ProtocolFor(protocol->Protocol())))
		return false;
			// a running connection may not change and there may only be
			// one authenticator protocol for each protocol number
	
	fProtocols.AddItem(protocol);
	
	if(IsUp() || Phase() >= protocol->Phase())
		protocol->Up();
	
	return true;
}


bool
PPPInterface::RemoveProtocol(PPPProtocol *protocol)
{
	LockerHelper locker(fLock);
	
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	if(!fProtocols.HasItem(protocol))
		return false;
	
	if(IsUp() || Phase() >= protocol->Phase())
		protocol->Down();
	
	return fProtocols.RemoveItem(protocol);
}


PPPProtocol*
PPPInterface::ProtocolAt(int32 index) const
{
	PPPProtocol *protocol = fProtocols.ItemAt(index);
	
	if(protocol == fProtocols.GetDefaultItem())
		return NULL;
	
	return protocol;
}


PPPProtocol*
PPPInterface::ProtocolFor(uint16 protocol, int32 *start = NULL) const
{
	// The iteration style in this method is strange C/C++.
	// Explanation: I use this style because it makes extending all XXXFor
	// methods simpler as that they look very similar, now.
	
	int32 index = start ? *start : 0;
	
	if(index < 0)
		return NULL;
	
	PPPProtocol *current = ProtocolAt(index);
	
	for(; current; current = ProtocolAt(++index)) {
		if(current->Protocol() == protocol
				|| (current->Flags() & PPP_INCLUDES_NCP
					&& current->Protocol() & 0x7FFF == protocol & 0x7FFF)) {
			if(start)
				*start = index;
			return current;
		}
	}
	
	return NULL;
}


bool
PPPInterface::AddEncapsulator(PPPEncapsulator *encapsulator)
{
	// Find instert position after the last encapsulator
	// with the same level.
	
	if(!encapsulator)
		return false;
	
	LockerHelper locker(fLock);
	
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	PPPEncapsulator *current = fFirstEncapsulator, *previous = NULL;
	
	while(current) {
		if(current->Level() < encapsulator->Level())
			break;
		
		previous = current;
		current = current->Next();
	}
	
	if(!current) {
		if(!previous)
			fFirstEncapsulator = encapsulator;
		else
			previous->SetNext(encapsulator);
		
		encapsulator->SetNext(NULL);
	} else {
		encapsulator->SetNext(current);
		
		if(!previous)
			fFirstEncapsulator = encapsulator;
		else
			previous->SetNext(encapsulator);
	}
	
	CalculateInterfaceMTU();
	
	if(IsUp())
		encapsulator->Up();
	
	return true;
}


bool
PPPInterface::RemoveEncapsulator(PPPEncapsulator *encapsulator)
{
	LockerHelper locker(fLock);
	
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	PPPEncapsulator *current = fFirstEncapsulator, *previous = NULL;
	
	while(current) {
		if(current == encapsulator) {
			if(IsUp())
				encapsulator->Down();
			
			if(previous)
				previous->SetNext(current->Next());
			else
				fFirstEncapsulator = current->Next();
			
			current->SetNext(NULL);
			
			CalculateInterfaceMTU();
			
			return true;
		}
		
		previous = current;
		current = current->Next();
	}
	
	return false;
}


int32
PPPInterface::CountEncapsulators() const
{
	PPPEncapsulator *encapsulator = FirstEncapsulator();
	int32 count = 0;
	for(; encapsulator; encapsulator = encapsulator->Next())
		++count;
	
	return count;
}


PPPEncapsulator*
PPPInterface::EncapsulatorAt(int32 index) const
{
	PPPEncapsulator *encapsulator = FirstEncapsulator();
	int32 currentIndex = 0;
	for(; encapsulator; encapsulator = encapsulator->Next(), ++currentIndex)
		if(currentIndex == index)
			return encapsulator;
	
	return NULL;
}


PPPEncapsulator*
PPPInterface::EncapsulatorFor(uint16 protocol, PPPEncapsulator *start = NULL) const
{
	PPPEncapsulator *current = start ? start : fFirstEncapsulator;
	
	for(; current; current = current->Next()) {
		if(current->Protocol() == protocol
				|| (current->Flags() & PPP_INCLUDES_NCP
					&& current->Protocol() & 0x7FFF == protocol & 0x7FFF))
			return current;
	}
	
	return current;
}


bool
PPPInterface::AddChild(PPPInterface *child)
{
	if(!child)
		return false;
	
	LockerHelper locker(fLock);
	
	if(fChildren.HasItem(child) || !fChildren.AddItem(child))
		return false;
	
	child->SetParent(this);
	
	return true;
}


bool
PPPInterface::RemoveChild(PPPInterface *child)
{
	LockerHelper locker(fLock);
	
	if(!fChildren.RemoveItem(child))
		return false;
	
	child->SetParent(NULL);
	
	// parents cannot exist without their children
	if(CountChildren() == 0 && fManager && Ifnet())
		Delete();
	
	return true;
}


PPPInterface*
PPPInterface::ChildAt(int32 index) const
{
	PPPInterface *child = fChildren.ItemAt(index);
	
	if(child == fChildren.GetDefaultItem())
		return NULL;
	
	return child;
}


void
PPPInterface::SetAutoRedial(bool autoredial = true)
{
	if(Mode() == PPP_CLIENT_MODE)
		return;
	
	LockerHelper locker(fLock);
	
	fAutoRedial = autoredial;
}


void
PPPInterface::SetDialOnDemand(bool dialondemand = true)
{
	// All protocols must check if DialOnDemand was enabled/disabled after this
	// interface went down. This is the only situation where a change is relevant.
	
	// only clients support DialOnDemand
	if(Mode() != PPP_CLIENT_MODE)
		return;
	
	LockerHelper locker(fLock);
	
	if(DoesDialOnDemand() && !dialondemand && State() != PPP_OPENED_STATE) {
		// as long as the protocols were not configured we can just delete us
		Delete();
		return;
	}
	
	fDialOnDemand = dialondemand;
	
	// check if we need to register/unregister
	if(fDialOnDemand) {
		RegisterInterface();
		if(Ifnet())
			Ifnet()->if_flags |= IFF_RUNNING;
	} else if(!fDialOnDemand && Phase() < PPP_ESTABLISHED_PHASE) {
		UnregisterInterface();
		
		// if we are already down we must delete us
		if(Phase() == PPP_DOWN_PHASE)
			Delete();
	}
}


bool
PPPInterface::SetPFCOptions(uint8 pfcOptions)
{
	if(PFCOptions() & PPP_FREEZE_PFC_OPTIONS)
		return false;
	
	fPFCOptions = pfcOptions;
	return true;
}


bool
PPPInterface::Up()
{
	if(InitCheck() != B_OK || Phase() == PPP_TERMINATION_PHASE)
		return false;
	
	if(IsUp())
		return true;
	
	ppp_report_packet report;
	thread_id me = find_thread(NULL), sender;
	
	// One thread has to do the real task while all other threads are observers.
	// Lock needs timeout because destructor could have locked the interface.
	while(!fLock.LockWithTimeout(100000) != B_NO_ERROR)
		if(fDeleteCounter > 0)
			return false;
	if(fUpThread == -1)
		fUpThread = me;
	
	ReportManager().EnableReports(PPP_CONNECTION_REPORT, me, PPP_WAIT_FOR_REPLY);
	fLock.Unlock();
	
	// fUpThread tells the state machine to go up
	if(me == fUpThread || me == fRedialThread)
		StateMachine().OpenEvent();
	
	if(me == fRedialThread && me != fUpThread)
		return true;
			// the redial thread is doing a DialRetry in this case
	
	while(true) {
		if(IsUp()) {
			// lock needs timeout because destructor could have locked the interface
			while(!fLock.LockWithTimeout(100000) != B_NO_ERROR)
				if(fDeleteCounter > 0)
					return true;
			
			if(me == fUpThread) {
				fDialRetry = 0;
				fUpThread = -1;
			}
			
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
			fLock.Unlock();
			
			return true;
		}
		
		// A wrong code usually happens when the redial thread gets notified
		// of a Down() request. In that case a report will follow soon, so
		// this can be ignored.
		if(receive_data(&sender, &report, sizeof(report)) != PPP_REPORT_CODE)
			continue;
		
		if(IsUp()) {
			if(me == fUpThread) {
				fDialRetry = 0;
				fUpThread = -1;
			}
			
			PPP_REPLY(sender, B_OK);
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
			return true;
		}
		
		if(report.type == PPP_DESTRUCTION_REPORT) {
			if(me == fUpThread) {
				fDialRetry = 0;
				fUpThread = -1;
			}
			
			PPP_REPLY(sender, B_OK);
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
			return false;
		} else if(report.type != PPP_CONNECTION_REPORT) {
			PPP_REPLY(sender, B_OK);
			continue;
		}
		
		if(report.code == PPP_REPORT_GOING_UP) {
			PPP_REPLY(sender, B_OK);
			continue;
		} else if(report.code == PPP_REPORT_UP_SUCCESSFUL) {
			if(me == fUpThread) {
				fDialRetry = 0;
				fUpThread = -1;
				send_data_with_timeout(fRedialThread, 0, NULL, 0, 200);
					// notify redial thread that we do not need it anymore
			}
			
			PPP_REPLY(sender, B_OK);
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
			return true;
		} else if(report.code == PPP_REPORT_DOWN_SUCCESSFUL
				|| report.code == PPP_REPORT_UP_ABORTED
				|| report.code == PPP_REPORT_LOCAL_AUTHENTICATION_FAILED
				|| report.code == PPP_REPORT_PEER_AUTHENTICATION_FAILED) {
			if(me == fUpThread) {
				fDialRetry = 0;
				fUpThread = -1;
				
				if(!DoesDialOnDemand() && report.code != PPP_REPORT_DOWN_SUCCESSFUL)
					Delete();
			}
			
			PPP_REPLY(sender, B_OK);
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
			return false;
		}
		
		if(me != fUpThread) {
			// I am an observer
			if(report.code == PPP_REPORT_DEVICE_UP_FAILED) {
				if(fDialRetry >= fDialRetriesLimit || fUpThread == -1) {
					PPP_REPLY(sender, B_OK);
					ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
					return false;
				} else {
					PPP_REPLY(sender, B_OK);
					continue;
				}
			} else if(report.code == PPP_REPORT_CONNECTION_LOST) {
				if(DoesAutoRedial()) {
					PPP_REPLY(sender, B_OK);
					continue;
				} else {
					PPP_REPLY(sender, B_OK);
					ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
					return false;
				}
			}
		} else {
			// I am the thread for the real task
			if(report.code == PPP_REPORT_DEVICE_UP_FAILED) {
				if(fDialRetry >= fDialRetriesLimit) {
					fDialRetry = 0;
					fUpThread = -1;
					
					if(!DoesDialOnDemand()
							&& report.code != PPP_REPORT_DOWN_SUCCESSFUL)
						Delete();
					
					PPP_REPLY(sender, B_OK);
					ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
					return false;
				} else {
					++fDialRetry;
					PPP_REPLY(sender, B_OK);
					Redial(DialRetryDelay());
					continue;
				}
			} else if(report.code == PPP_REPORT_CONNECTION_LOST) {
				// the state machine knows that we are going up and leaves
				// the redial task to us
				if(DoesAutoRedial() && fDialRetry < fDialRetriesLimit) {
					++fDialRetry;
					PPP_REPLY(sender, B_OK);
					Redial(DialRetryDelay());
					continue;
				} else {
					fDialRetry = 0;
					fUpThread = -1;
					PPP_REPLY(sender, B_OK);
					ReportManager().DisableReports(PPP_CONNECTION_REPORT, me);
					
					if(!DoesDialOnDemand()
							&& report.code != PPP_REPORT_DOWN_SUCCESSFUL)
						Delete();
					
					return false;
				}
			}
		}
		
		// if the code is unknown we continue
		PPP_REPLY(sender, B_OK);
	}
	
	return false;
}


bool
PPPInterface::Down()
{
	if(InitCheck() != B_OK)
		return false;
	
	send_data_with_timeout(fRedialThread, 0, NULL, 0, 200);
		// the redial thread should be notified that the user wants to disconnect
	
	// this locked section guarantees that there are no state changes before we
	// enable the connection reports
	LockerHelper locker(fLock);
	if(State() == PPP_INITIAL_STATE && Phase() == PPP_DOWN_PHASE)
		return true;
	
	ReportManager().EnableReports(PPP_CONNECTION_REPORT, find_thread(NULL));
	locker.UnlockNow();
	
	thread_id sender;
	ppp_report_packet report;
	
	StateMachine().CloseEvent();
	
	while(true) {
		if(receive_data(&sender, &report, sizeof(report)) != PPP_REPORT_CODE)
			continue;
		
		if(report.type == PPP_DESTRUCTION_REPORT)
			return true;
		
		if(report.type != PPP_CONNECTION_REPORT)
			continue;
		
		if(report.code == PPP_REPORT_DOWN_SUCCESSFUL
				|| report.code == PPP_REPORT_UP_ABORTED
				|| (State() == PPP_INITIAL_STATE && Phase() == PPP_DOWN_PHASE)) {
			ReportManager().DisableReports(PPP_CONNECTION_REPORT, find_thread(NULL));
			break;
		}
	}
	
	if(!DoesDialOnDemand())
		Delete();
	
	return true;
}


bool
PPPInterface::IsUp() const
{
	LockerHelper locker(fLock);
	
	return Phase() == PPP_ESTABLISHED_PHASE;
}


bool
PPPInterface::LoadModules(driver_settings *settings, int32 start, int32 count)
{
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	ppp_module_key_type type;
		// which type key was used for loading this module?
	
	const char *name = NULL;
	
	// multilink handling
	for(int32 index = start;
			index < settings->parameter_count && index < start + count; index++) {
		if(!strcasecmp(settings->parameters[index].name, PPP_MULTILINK_KEY)
				&& settings->parameters[index].value_count > 0) {
			if(!LoadModule(settings->parameters[index].values[0],
					settings->parameters[index].parameters, PPP_MULTILINK_TYPE))
				return false;
			break;
		}
		
	}
	
	// are we a multilink main interface?
	if(IsMultilink() && !Parent()) {
		// main interfaces only load the multilink module
		// and create a child using their settings
		fManager->create_interface(settings, ID());
		return true;
	}
	
	for(int32 index = start;
			index < settings->parameter_count && index < start + count; index++) {
		type = PPP_UNDEFINED_KEY_TYPE;
		
		name = settings->parameters[index].name;
		
		if(!strcasecmp(name, PPP_LOAD_MODULE_KEY))
			type = PPP_LOAD_MODULE_TYPE;
		else if(!strcasecmp(name, PPP_DEVICE_KEY))
			type = PPP_DEVICE_TYPE;
		else if(!strcasecmp(name, PPP_PROTOCOL_KEY))
			type = PPP_PROTOCOL_TYPE;
		else if(!strcasecmp(name, PPP_AUTHENTICATOR_KEY))
			type = PPP_AUTHENTICATOR_TYPE;
		else if(!strcasecmp(name, PPP_PEER_AUTHENTICATOR_KEY))
			type = PPP_PEER_AUTHENTICATOR_TYPE;
		
		if(type >= 0)
			for(int32 value_id = 0; value_id < settings->parameters[index].value_count;
					value_id++)
				if(!LoadModule(settings->parameters[index].values[value_id],
						settings->parameters[index].parameters, type))
					return false;
	}
	
	return true;
}


bool
PPPInterface::LoadModule(const char *name, driver_parameter *parameter,
	ppp_module_key_type type)
{
	if(Phase() != PPP_DOWN_PHASE)
		return false;
			// a running connection may not change
	
	if(!name || strlen(name) > B_FILE_NAME_LENGTH)
		return false;
	
	char *module_name = (char*) malloc(B_PATH_NAME_LENGTH);
	
	sprintf(module_name, "%s/%s", PPP_MODULES_PATH, name);
	
	ppp_module_info *module;
	if(get_module(module_name, (module_info**) &module) != B_OK)
		return false;
	
	// add the module to the list of loaded modules
	// for putting them on our destruction
	fModules.AddItem(module_name);
	
	return module->add_to(Parent()?*Parent():*this, this, parameter, type);
}


status_t
PPPInterface::Send(struct mbuf *packet, uint16 protocol)
{
	if(!packet)
		return B_ERROR;
	
	// we must pass the basic tests!
	if(InitCheck() != B_OK) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// test whether are going down
	if(Phase() == PPP_TERMINATION_PHASE) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// go up if DialOnDemand enabled and we are down
	if(DoesDialOnDemand() && Phase() == PPP_DOWN_PHASE)
		Up();
	
	// find the protocol handler for the current protocol number
	int32 index = 0;
	PPPProtocol *sending_protocol = ProtocolFor(protocol, &index);
	while(sending_protocol && !sending_protocol->IsEnabled())
		sending_protocol = ProtocolFor(protocol, &(++index));
	
	// Check if we must encapsulate the packet.
	// We do not necessarily need a handler for 'protocol'.
	// In such a case we still encapsulate the packet.
	// Protocols which have the PPP_ALWAYS_ALLOWED flag set are never
	// encapsulated.
	if(sending_protocol && sending_protocol->Flags() & PPP_ALWAYS_ALLOWED
			&& is_handler_allowed(*sending_protocol, State(), Phase())) {
		fIdleSince = real_time_clock();
		return SendToDevice(packet, protocol);
	}
	
	// try the same for the encapsulator handler
	PPPEncapsulator *sending_encapsulator = EncapsulatorFor(protocol);
	while(sending_encapsulator && sending_encapsulator->Next()
			&& !sending_encapsulator->IsEnabled())
		sending_encapsulator = sending_encapsulator->Next() ?
			EncapsulatorFor(protocol, sending_encapsulator->Next()) : NULL;
	
	if(sending_encapsulator && sending_encapsulator->Flags() & PPP_ALWAYS_ALLOWED
			&& is_handler_allowed(*sending_encapsulator, State(), Phase())) {
		fIdleSince = real_time_clock();
		return SendToDevice(packet, protocol);
	}
	
	// never send normal protocols when we are not up
	if(!IsUp()) {
		m_freem(packet);
		return B_ERROR;
	}
	
	fIdleSince = real_time_clock();
	
	// send to next up encapsulator
	if(!fFirstEncapsulator)
		return SendToDevice(packet, protocol);
	
	if(!fFirstEncapsulator->IsEnabled())
		return fFirstEncapsulator->SendToNext(packet, protocol);
	
	if(is_handler_allowed(*fFirstEncapsulator, State(), Phase()))
		return fFirstEncapsulator->Send(packet, protocol);
	
	m_freem(packet);
	return B_ERROR;
}


status_t
PPPInterface::Receive(struct mbuf *packet, uint16 protocol)
{
	if(!packet)
		return B_ERROR;
	
	int32 result = PPP_REJECTED;
		// assume we have no handler
	
	// Set our interface as the receiver.
	// The real netstack protocols (IP, IPX, etc.) might get confused if our
	// interface is a main interface and at the same time is not registered
	// because then there is no receiver interface.
	// PPP NCPs should be aware of that!
	if(packet->m_flags & M_PKTHDR && Ifnet() != NULL)
		packet->m_pkthdr.rcvif = Ifnet();
	
	// Find handler and let it parse the packet.
	// The handler does need not be up because if we are a server
	// the handler might be upped by this packet.
	PPPEncapsulator *encapsulator_handler = EncapsulatorFor(protocol);
	for(; encapsulator_handler;
			encapsulator_handler =
				encapsulator_handler->Next() ?
					EncapsulatorFor(protocol, encapsulator_handler->Next()) : NULL) {
		if(!encapsulator_handler->IsEnabled()
				|| !is_handler_allowed(*encapsulator_handler, State(), Phase()))
			continue;
				// skip handler if disabled or not allowed
		
		result = encapsulator_handler->Receive(packet, protocol);
		if(result == PPP_UNHANDLED)
			continue;
		
		return result;
	}
	
	// no encapsulator handler could be found; try a protocol handler
	int32 index = 0;
	PPPProtocol *protocol_handler = ProtocolFor(protocol, &index);
	for(; protocol_handler; protocol_handler = ProtocolFor(protocol, &(++index))) {
		if(!protocol_handler->IsEnabled()
				|| !is_handler_allowed(*protocol_handler, State(), Phase()))
			continue;
				// skip handler if disabled or not allowed
		
		result = protocol_handler->Receive(packet, protocol);
		if(result == PPP_UNHANDLED)
			continue;
		
		return result;
	}
	
	// maybe the parent interface can handle the packet
	if(Parent())
		return Parent()->Receive(packet, protocol);
	
	if(result == PPP_UNHANDLED) {
		m_freem(packet);
		return PPP_DISCARDED;
	} else {
		StateMachine().RUCEvent(packet, protocol);
		return PPP_REJECTED;
	}
}


status_t
PPPInterface::SendToDevice(struct mbuf *packet, uint16 protocol)
{
	if(!packet)
		return B_ERROR;
	
	// we must pass the basic tests like:
	// do we have a device?
	// did we load all modules?
	if(InitCheck() != B_OK || !Device()) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// test whether are going down
	if(Phase() == PPP_TERMINATION_PHASE) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// go up if DialOnDemand enabled and we are down
	if(DoesDialOnDemand()
			&& (Phase() == PPP_DOWN_PHASE
				|| Phase() == PPP_ESTABLISHMENT_PHASE)
			&& !Up()) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// find the protocol handler for the current protocol number
	int32 index = 0;
	PPPProtocol *sending_protocol = ProtocolFor(protocol, &index);
	while(sending_protocol && !sending_protocol->IsEnabled())
		sending_protocol = ProtocolFor(protocol, &(++index));
	
	// make sure that protocol is allowed to send and everything is up
	if(!Device()->IsUp() || !sending_protocol || !sending_protocol->IsEnabled()
			|| !is_handler_allowed(*sending_protocol, State(), Phase())){
		m_freem(packet);
		return B_ERROR;
	}
	
	// encode in ppp frame and consider using PFC
	if(UseLocalPFC() && protocol & 0xFF00 == 0) {
		M_PREPEND(packet, 1);
		
		if(packet == NULL)
			return B_ERROR;
		
		uint8 *header = mtod(packet, uint8*);
		*header = protocol & 0xFF;
	} else {
		M_PREPEND(packet, 2);
		
		if(packet == NULL)
			return B_ERROR;
		
		// set protocol (the only header field)
		protocol = htons(protocol);
		uint16 *header = mtod(packet, uint16*);
		*header = protocol;
	}
	
	fIdleSince = real_time_clock();
	
	// pass to device/children
	if(!IsMultilink() || Parent()) {
		// check if packet is too big for device
		if((packet->m_flags & M_PKTHDR && (uint32) packet->m_pkthdr.len > MRU())
				|| packet->m_len > MRU()) {
			m_freem(packet);
			return B_ERROR;
		}
		
		return Device()->Send(packet);
	} else {
		// the multilink encapsulator should have sent it to some child interface
		m_freem(packet);
		return B_ERROR;
	}
}


status_t
PPPInterface::ReceiveFromDevice(struct mbuf *packet)
{
	if(!packet)
		return B_ERROR;
	
	if(InitCheck() != B_OK) {
		m_freem(packet);
		return B_ERROR;
	}
	
	// decode ppp frame and recognize PFC
	uint16 protocol = *mtod(packet, uint8*);
	if(protocol == 0)
		m_adj(packet, 1);
	else {
		protocol = ntohs(*mtod(packet, uint16*));
		m_adj(packet, 2);
	}
	
	return Receive(packet, protocol);
}


void
PPPInterface::Pulse()
{
	if(fDeleteCounter > 0)
		return;
			// we have no pulse when we are dead ;)
	
	// check our idle time and disconnect if needed
	if(fDisconnectAfterIdleSince > 0 && fIdleSince != 0
			&& fIdleSince - real_time_clock() >= fDisconnectAfterIdleSince) {
		StateMachine().CloseEvent();
		return;
	}
	
	if(Device())
		Device()->Pulse();
	
	for(int32 index = 0; index < CountProtocols(); index++)
		ProtocolAt(index)->Pulse();
	
	PPPEncapsulator *encapsulator = fFirstEncapsulator;
	for(; encapsulator; encapsulator = encapsulator->Next())
		encapsulator->Pulse();
}


bool
PPPInterface::RegisterInterface()
{
	if(fIfnet)
		return true;
			// we are already registered
	
	LockerHelper locker(fLock);
	
	if(InitCheck() != B_OK)
		return false;
			// we cannot register if something is wrong
	
	// only MainInterfaces get an ifnet
	if(IsMultilink() && Parent() && Parent()->RegisterInterface())
		return true;
	
	if(!fManager)
		return false;
	
	fIfnet = fManager->register_interface(ID());
	
	if(!fIfnet)
		return false;
	
	CalculateBaudRate();
	SetupDialOnDemand();
	
	return true;
}


bool
PPPInterface::UnregisterInterface()
{
	if(!fIfnet)
		return true;
			// we are already unregistered
	
	LockerHelper locker(fLock);
	
	// only MainInterfaces get an ifnet
	if(IsMultilink() && Parent())
		return true;
	
	if(!fManager)
		return false;
	
	fManager->unregister_interface(ID());
	fIfnet = NULL;
	
	return true;
}


// stack routes ioctls to interface
status_t
PPPInterface::StackControl(uint32 op, void *data)
{
	// TODO:
	// implement when writing ppp_manager module
	
	switch(op) {
		default:
			return B_BAD_VALUE;
	}
	
	return B_OK;
}


// This calls Control() with the given parameters for each handler.
// Return values:
//  B_OK: all handlers returned B_OK
//  B_BAD_VALUE: no handler was found
//  any other value: the error value that was returned by the last handler that failed
status_t
PPPInterface::ControlEachHandler(uint32 op, void *data, size_t length)
{
	int32 index;
	status_t result = B_BAD_VALUE, tmp;
	
	// protocols
	PPPProtocol *protocol;
	for(index = 0; index < CountProtocols(); index++) {
		protocol = ProtocolAt(index);
		if(!protocol)
			break;
		
		tmp = protocol->Control(op, data, length);
		if(tmp == B_OK && result == B_BAD_VALUE)
			result = B_OK;
		else if(tmp != B_BAD_VALUE)
			result = tmp;
	}
	
	// encapsulators
	PPPEncapsulator *encapsulator = FirstEncapsulator();
	for(; encapsulator; encapsulator = encapsulator->Next()) {
		tmp = encapsulator->Control(op, data, length);
		if(tmp == B_OK && result == B_BAD_VALUE)
			result = B_OK;
		else if(tmp != B_BAD_VALUE)
			result = tmp;
	}
	
	// option handlers
	PPPOptionHandler *optionHandler;
	for(index = 0; index < LCP().CountOptionHandlers(); index++) {
		optionHandler = LCP().OptionHandlerAt(index);
		if(!optionHandler)
			break;
		
		tmp = optionHandler->Control(op, data, length);
		if(tmp == B_OK && result == B_BAD_VALUE)
			result = B_OK;
		else if(tmp != B_BAD_VALUE)
			result = tmp;
	}
	
	// LCP extensions
	PPPLCPExtension *lcpExtension;
	for(index = 0; index < LCP().CountLCPExtensions(); index++) {
		lcpExtension = LCP().LCPExtensionAt(index);
		if(!lcpExtension)
			break;
		
		tmp = lcpExtension->Control(op, data, length);
		if(tmp == B_OK && result == B_BAD_VALUE)
			result = B_OK;
		else if(tmp != B_BAD_VALUE)
			result = tmp;
	}
	
	return result;
}


void
PPPInterface::CalculateInterfaceMTU()
{
	fInterfaceMTU = fMRU;
	
	// sum all headers
	fHeaderLength = sizeof(uint16);
	
	PPPEncapsulator *encapsulator = fFirstEncapsulator;
	for(; encapsulator; encapsulator = encapsulator->Next())
		fHeaderLength += encapsulator->Overhead();
	
	fInterfaceMTU -= fHeaderLength;
	
	if(Ifnet()) {
		Ifnet()->if_mtu = fInterfaceMTU;
		Ifnet()->if_hdrlen = fHeaderLength;
	}
	
	if(Parent())
		Parent()->CalculateInterfaceMTU();
}


void
PPPInterface::CalculateBaudRate()
{
	if(!Ifnet())
		return;
	
	if(Device())
		fIfnet->if_baudrate = max_c(Device()->InputTransferRate(),
			Device()->OutputTransferRate());
	else {
		fIfnet->if_baudrate = 0;
		for(int32 index = 0; index < CountChildren(); index++)
			if(ChildAt(index)->Ifnet())
				fIfnet->if_baudrate += ChildAt(index)->Ifnet()->if_baudrate;
	}
}


bool
PPPInterface::SetupDialOnDemand()
{
	LockerHelper locker(fLock);
	
	if(State() == PPP_OPENED_STATE)
		return true;
	
	bool result = true;
	
	PPPProtocol *protocol;
	for(int32 index = 0; index < CountProtocols(); index++) {
		protocol = ProtocolAt(index);
		if(protocol && protocol->IsEnabled())
			if(protocol->SetupDialOnDemand() != B_OK)
				result = false;
	}
	
	return result;
}


void
PPPInterface::Redial(uint32 delay)
{
	if(fRedialThread != -1)
		return;
	
	// start a new thread that calls our Up() method
	redial_info info;
	info.interface = this;
	info.thread = &fRedialThread;
	info.delay = delay;
	
	fRedialThread = spawn_thread(redial_thread, "PPPInterface: redial_thread",
		B_NORMAL_PRIORITY, NULL);
	
	resume_thread(fRedialThread);
	
	send_data(fRedialThread, 0, &info, sizeof(redial_info));
}


status_t
redial_thread(void *data)
{
	redial_info info;
	thread_id sender;
	int32 code;
	
	receive_data(&sender, &info, sizeof(redial_info));
	
	// we try to receive data instead of snooze, so we can quit on destruction
	if(receive_data_with_timeout(&sender, &code, NULL, 0, info.delay) == B_OK) {
		*info.thread = -1;
		info.interface->Report(PPP_CONNECTION_REPORT, PPP_REPORT_UP_ABORTED, NULL, 0);
		return B_OK;
	}
	
	info.interface->Up();
	*info.thread = -1;
	
	return B_OK;
}


status_t
in_queue_thread(void *data)
{
	PPPInterface *interface = (PPPInterface*) data;
	struct ifq *queue = interface->InQueue();
	struct mbuf *packet;
	status_t error;
	
	while(true) {
		error = acquire_sem_etc(queue->pop, 1, B_CAN_INTERRUPT | B_DO_NOT_RESCHEDULE, 0);
		
		if(error == B_INTERRUPTED)
			continue;
		else if(error != B_NO_ERROR)
			break;
		
		IFQ_DEQUEUE(queue, packet);
		
		if(packet)
			interface->ReceiveFromDevice(packet);
	}
	
	return B_ERROR;
}


status_t
interface_deleter_thread(void *data)
{
	delete (PPPInterface*) data;
	
	return B_OK;
}
