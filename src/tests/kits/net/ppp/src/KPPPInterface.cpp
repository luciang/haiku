#include "KPPPInterface.h"

// our other classes
#include "KPPPModule.h"
#include "KPPPManager.h"

// general helper classes not only belonging to us
#include "AccessHelper.h"
#include "LockerHelper.h"

// tools only for us :)
#include "KPPPUtils.h"
#include "settings_tools.h"

#include <cstring>


// TODO:
// - report module errors (when loading in ctor)
//    (in InitCheck(), too)
// - add free-list for driver_settings that were loaded by Control()
// - implement timers
// - maybe some protocols must go down instead of being reset -> add flag for this


PPPInterface::PPPInterface(driver_settings *settings)
	: fSettings(dup_driver_settings(settings)),
	FSM(*this), LCP(*this), fIfnet(NULL), fLinkMTU(0),
	fAccessing(0), fDevice(NULL), fFirstEncapsulator(NULL),
	fGeneralLock(FSM().Locker())
{
	if(!fSettings)
		return;
	
	const char *value;
	
	value = get_settings_value(PPP_MODE_KEY, fsettings);
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
	LoadModules(fSettings, 0, fSettings->parameter_count);
	
	FSM().LeaveConstructionPhase();
}


PPPInterface::~PPPInterface()
{
	// TODO:
	// remove our iface, so that nobody will access it:
	//  go down if up
	//  unregister from ppp_manager
	
	FSM().EnterDestructionPhase();
	
	// destroy and remove:
	// device
	// protocols
	// encapsulators
	// option handlers
	
	// put all modules (in fModules)
}


status_t
PPPInterface::InitCheck() const
{
	if(!fDevice
		|| !fSettings)
		return B_ERROR;
	
	return B_OK;
}


bool
PPPInterface::RegisterInterface()
{
	if(fIfnet)
		return true;
			// we are already registered
	
	if(!InitCheck())
		return false;
			// we cannot register if something is wrong
	
	ppp_manager_info *manager;
	if(get_module(PPP_MANAGER_MODULE_NAME, (module_info**) &manager) != B_OK)
		return false;
	
	fIfnet = manager->add_interface(this);
	
	put_module((module_info**) &manager);
	
	
	if(!fIfnet)
		return false;
	
	return true;
}


bool
PPPInterface::UnregisterInterface()
{
	if(!fIfnet)
		return true;
			// we are already unregistered
	
	ppp_manager_info *manager;
	if(get_module(PPP_MANAGER_MODULE_NAME, (module_info**) &manager) != B_OK)
		return false;
	
	manager->remove_interface(this);
	fIfnet = NULL;
	
	put_module((module_info**) &manager);
	
	return true;
}


void
PPPInterface::SetLinkMTU(uint32 linkMTU)
{
	if(linkMTU < fLinkMTU && linkMTU > 234)
		fLinkMTU = linkMTU;
}


status_t
PPPInterface::Control(uint32 op, void *data, size_t length)
{
	switch(op) {
		// TODO:
		// add:
		// - routing Control() to encapsulators/protocols/option_handlers
		//    (calling their Control() method)
		// - adding modules in right mode
		// - setting AutoRedial and DialOnDemand
		
		
		default:
		return B_ERROR;
	}
	
	return B_OK;
}


bool
PPPInterface::SetDevice(PPPDevice *device)
{
	if(!device)
		return false;
	
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	if(fDevice && (IsUp() || fDevice->IsUp()))
		Down();
	
	fDevice = device;
	
	fLinkMTU = fDevice->MTU();
	
	CalculateMRU();
}


bool
PPPInterface::AddProtocol(PPPProtocol *protocol)
{
	if(!protocol)
		return false;
	
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	fProtocols.AddItem(protocol);
	
	if(IsUp() || Phase() >= protocol->Phase())
		protocol->Up();
}


bool
PPPInterface::RemoveProtocol(PPPProtocol *protocol)
{
	if(Phase() != PPP_CTOR_DTOR_PHASE)
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
PPPInterface::ProtocolFor(uint16 protocol, int32 start = 0) const
{
	if(start < 0)
		return NULL;
	
	for(int32 i = start; i < fProtocols.CountItems(); i++)
		if(fProtocols.ItemAt(i)->Protocol() == protocol)
			return fProtocols.ItemAt(i);
	
	return NULL;
}


bool
PPPInterface::AddEncapsulator(PPPEncapsulator *encapsulator)
{
	// Find instert position after the last encapsulator
	// with the same level.
	
	if(!encapsulator)
		return false;
	
	if(Phase() != PPP_CTOR_DTOR_PHASE)
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
	
	CalculateMRU();
	
	if(IsUp())
		encapsulator->Up();
	
	return true;
}


bool
PPPInterface::RemoveEncapsulator(PPPEncapsulator *encapsulator)
{
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	PPPEncapsulator *current = fFirstEncapsulator, previous = NULL;
	
	while(current) {
		if(current == encapsulator) {
			if(IsUp())
				encapsulator->Down();
			
			if(previous)
				previous->SetNext(current->Next());
			else
				fFirstEncapsulator = current->Next();
			
			current->SetNext(NULL);
			
			CalculateMRU();
			
			return true;
		}
		
		previous = current;
		current = current->Next();
	}
	
	return false;
}


PPPEncapsulator*
PPPInterface::EncapsulatorFor(uint16 protocol,
	PPPEncapsulator start = NULL) const
{
	PPPEncapsulator *current = start ? start : fFirstEncapsulator;
	
	for(; current; current = current->Next())
		if(current->Protocol() == protocol)
			return current;
	
	return current;
}


bool
PPPInterface::AddOptionHandler(PPPOptionHandler *handler)
{
	if(!handler)
		return false
	
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	fOptionHandlers.AddItem(handler);
}


bool
PPPInterface::RemoveOptionHandler(PPPOptionHandler *handler)
{
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	return fOptionHandlers.RemoveItem(handler);
}


PPPOptionHandler*
PPPInterface::OptionHandlerAt(int32 index) const
{
	PPPOptionHandler *handler = fOptionHandlers.ItemAt(index);
	
	if(handler == fOptionHandlers.DefaultItem())
		return NULL;
	
	return handler;
}


void
PPPInterface::SetAutoRedial(bool autoredial = true)
{
	if(Mode() == PPP_CLIENT_MODE)
		return false;
	
	LockerHelper locker(fGeneralLock);
	
	fAutoRedial = autoredial;
}


void
PPPInterface::SetDialOnDemand(bool dialondemand = true)
{
	if(Mode() != PPP_CLIENT_MODE)
		return false;
	
	LockerHelper locker(fGeneralLock);
	
	fDialOnDemand = dialondemand;
	
	// check if we need to register/unregister
	if(!Ifnet() && fDialOnDemand)
		RegisterInterface();
	else if(Ifnet() && !fDialOnDemand && Phase() == PPP_DOWN_PHASE)
		UnregisterInterface();
}


bool
PPPInterface::Up()
{
	// ToDo:
	// instead of waiting for state change we should wait until
	// all retries are done
	
	if(!InitCheck() || Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
	
	if(IsUp())
		return true;
	
	// TODO:
	// Add one-time connection report request and wait
	// for results. If we lost the connection we should
	// consider redialing.
	
	return false;
}


bool
PPPInterface::Down()
{
	// ToDo:
	// instead of waiting for state change we should wait until
	// all retries are done
	
	if(!InitCheck() || Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
	
	// TODO:
	// Add one-time connection report request.
	
	return false;
}


bool
PPPInterface::IsUp() const
{
	LockerHelper locker(fGeneralLock);
	if(Ifnet())
		return Ifnet()->if_flags & IFF_RUNNING;
	
	return false;
}


/*

void
PPPInterface::EnableReports(PPP_REPORT type, thread_id thread,
	bool needsVerification = false)
{
}


void
PPPInterface::DisableReports(PPP_REPORT type, thread_id thread)
{
}


bool
PPPInterface::Reports(PPP_REPORT type, thread_id thread)
{
}

*/


bool
PPPInterface::LoadModules(const driver_settings *settings,
	int32 start, int32 count)
{
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	int32 type;
		// which type key was used for loading this module?
	
	for(int32 i = start;
		i < settings->parameter_count && i < start + count;
		i++) {
		
		type = -1;
		
		const char *name = settings->parameters[i].name;
		
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
			for(int32 value_id = 0; value_id < settings->parameters[i].value_count;
				value_id++)
				if(!LoadModule(settings->parameters[i].value[value_id],
				settings->parameters[i].parameters, type))
					return false;
	}
	
	return true;
}


bool
PPPInterface::LoadModule(const char *name, const driver_parameter *parameter,
	int32 type)
{
	if(Phase() != PPP_CTOR_DTOR_PHASE)
		return false;
			// a running connection may not change
	
	if(!name || strlen(name) > B_FILE_NAME_LENGTH)
		return false;
	
	char module_name[B_PATH_NAME_LENGTH];
	
	sprintf(module_name, "%s/%s", PPP_MODULES_PATH, name);
	
	ppp_module_info *module;
	if(get_module(module_name, (module_info**) &module) != B_OK)
		return false;
	
	if(!module->add_to(this, parameter, type))
		return false;
	
	// add the module to the list of loaded modules
	// for putting them on our destruction
	return fModules.AddItem(module);
}


status_t
PPPInterface::Send(mbuf *packet, uint16 protocol)
{
	AccessHelper access(&fAccessing);
	
	if(!packet)
		return B_ERROR;
	
	// we must pass the basic tests!
	if(InitCheck() != B_OK) {
		m_free(packet);
		return B_ERROR;
	}
	
	// test whether are going down
	if(Phase() == PPP_TERMINATION_PHASE) {
		m_free(packet);
		return B_ERROR;
	}
	
	// go up if DialOnDemand enabled and we are down
	if(DoesDialOnDemand() && Phase() == PPP_DOWN_PHASE)
		Up();
	
	// If this protocol is always allowed we should send the packet.
	// Note that these protocols are not passed to encapsulators!
	PPPProtocol *sending_protocol = ProtocolFor(protocol);
	if(sending_protocol && sending_protocol->Flags() & PPP_ALWAYS_ALLOWED
		&& sending_protocol->IsEnabled() && fDevice->IsUp())
		return SendToDevice(packet, protocol);
	
	// never send normal protocols when we are down
	if(!IsUp()) {
		m_free(packet);
		return B_ERROR;
	}
	
	// send to next up encapsulator
	if(!fFirstEncapsulator)
		return SendToDevice(packet, protocol);
	
	if(!fFirstEncapsulator->IsEnabled())
		return fFirstEncapsulator->SendToNext();
	
	return fFirstEncapsulator->Send(packet, protocol);
}


status_t
PPPInterface::Receive(mbuf *packet, uint16 protocol)
{
	AccessHelper access(&fAccessing);
	
	if(!packet)
		return B_ERROR;
	
	int32 result = PPP_REJECTED;
		// assume we have no handler
	
	// Find handler and let it parse the packet.
	// The handler does need not be up because if we are a server
	// the handler might be upped by this packet.
	PPPEncapsulator *encapsulator_handler = EncapsulatorFor(protocol);
	for(; encapsulator_handler;
		encapsulator_handler = EncapsulatorFor(protocol, encapsulator_handler)) {
		if(!encapsulator_handler->IsEnabled()) {
			// disabled handlers should not be used
			result = PPP_REJECTED;
			continue;
		}
		
		result = encapsulator_handler->Receive(packet, protocol);
		if(result == PPP_UNHANDLED)
			continue;
		
		return result;
	}
	
	// no encapsulator handler could be found; try a protocol handler
	PPPProtocol *protocol_handler;
	for(int32 index = 0; index < CountProtocols(); index++) {
		protocol_handler = ProtocolAt(index);
		if(protocol != protocol_handler->Protocol())
			continue;
		
		if(!protocol_handler->IsEnabled()) {
			// disabled handlers should not be used
			result = PPP_REJECTED;
			continue;
		}
		
		result = protocol_handler->Receive(packet, protocol);
		if(result == PPP_UNHANDLED)
			continue;
		
		return result;
	}
	
	// packet is unhandled
	m_free(packet);
	
	if(result == PPP_UNHANDLED)
		return PPP_DISCARDED;
	else {
		// TODO:
		// send protocol-reject!
		return PPP_REJECTED;
	}
}


status_t
PPPInterface::SendToDevice(mbuf *packet, uint16 protocol)
{
	AccessHelper access(&fAccessing);
	
	if(!packet)
		return B_ERROR;
	
	// we must pass the basic tests like:
	// do we have a device?
	// did we load all modules?
	if(InitCheck() != B_OK) {
		m_free(packet);
		return B_ERROR;
	}
	
	// test whether are going down
	if(Phase() == PPP_TERMINATION_PHASE) {
		m_free(packet);
		return B_ERROR;
	}
	
	// go up if DialOnDemand enabled and we are down
	if(DoesDialOnDemand() && Phase() == PPP_DOWN_PHASE)
		Up();
	
	// check if protocol is allowed and everything is up
	PPPProtocol *sending_protocol = ProtocolFor(protocol);
	if(!fDevice->IsUp()
		|| (!IsUp() && protocol != PPP_LCP_PROTOCOL
			&& (!sending_protocol
				|| sending_protocol->Flags() & PPP_ALWAYS_ALLOWED == 0
				|| !sending_protocol->IsEnabled()
				)
			)
		) {
		m_free(packet);
		return B_ERROR;
	}
	
	// encode in ppp frame
	M_PREPEND(packet, sizeof(uint16));
	
	if(packet == NULL)
		return B_ERROR;
	
	// check if packet is too big
	if((packet->m_flags & M_PKTHDR && packet->m_pkt_hdr.len > LinkMTU())
		|| packet->m_len > LinkMTU()) {
		m_free(packet);
		return B_ERROR;
	}
	
	// set protocol (the only header field)
	protocol = htons(protocol);
	uint16 *header = mtod(packet, uint16*);
	*header = protocol;
	
	// pass to device
	return Device()->Send(packet);
}


status_t
PPPInterface::ReceiveFromDevice(mbuf *packet)
{
	AccessHelper access(&fAccessing);
	
	if(!packet)
		return B_ERROR;
	
	if(!InitCheck()) {
		m_free(packet);
		return B_ERROR;
	}
	
	// decode ppp frame
	uint16 *protocol = mtod(packet, uint16*);
	*protocol = ntohs(*protocol);
	
	m_adj(packet, sizeof(uint16));
	
	if(packet->m_flags & M_PKTHDR) {
		packet->m_pkthdr.rcvif = Ifnet();
			// make sure the protocols think we received
			// the packet
		
		if(!packet->m_pkthdr.rcvif) {
			m_free(packet);
			return false;
		}
	}
	
	return Receive(packet, protocol);
}


void
PPPInterface::CalculateMRU()
{
	fMRU = fLinkMTU;
	
	// sum all headers
	fHeaderLength = sizeof(uint16);
	
	PPPEncapsulator encapsulator = fFirstEncapsulator;
	while(encapsulator) {
		fHeaderLength += encapsulator->Overhead();
		encapsulator = encapsulator->Next();
	}
	
	fMRU -= fHeaderLength;
	
	if(Ifnet()) {
		Ifnet()->if_mtu = fMRU;
		Ifnet()->if_hdrlen = fHeaderLength;
	}
}
