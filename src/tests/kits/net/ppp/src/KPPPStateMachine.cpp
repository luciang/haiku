#include "KPPPStateMachine.h"


// TODO:
// do not forget to reset authentication status when:
// - connection is lost
// - reconfiguring?
// - terminating
// - ...

PPPStateMachine::PPPStateMachine(PPPInterface& interface)
	: fInterface(&interface), fPhase(PPP_DOWN_PHASE),
	fState(PPP_INITIAL_STATE), fID(system_time() & 0xFF),
	fAuthenticationStatus(PPP_NOT_AUTHENTICATED),
	fPeerAuthenticationStatus(PPP_NOT_AUTHENTICATED),
	fAuthenticationName(NULL), fPeerAuthenticationName(NULL),
	fAuthenticatorIndex(-1), fPeerAuthenticatorIndex(-1),
	fMaxTerminate(2), fMaxConfigure(10), fMaxNak(5),
	fRequestID(0), fTerminateID(0), fTimeout(0)
{
}


PPPStateMachine::~PPPStateMachine()
{
	free(fAuthenticationName);
	free(fPeerAuthenticationName);
}


uint8
PPPStateMachine::NextID()
{
	return (uint8) atomic_add(&fID, 1);
}


// remember: NewState() must always be called _after_ IllegalEvent()
// because IllegalEvent() also looks at the current state.
void
PPPStateMachine::NewState(PPP_STATE next)
{
	// TODO:
//	if(State() == PPP_OPENED_STATE)
//		ResetOptionHandlers();
	
	fState = next;
}


void
PPPStateMachine::NewPhase(PPP_PHASE next)
{
	// Report a down event to parent if we are not usable anymore.
	// The report threads get their notification later.
	if(Phase() == PPP_ESTABLISHED_PHASE && next != Phase()) {
		if(Interface()->Ifnet())
			Interface()->Ifnet()->if_flags &= ~IFF_RUNNING;
		
		if(!Interface()->DoesDialOnDemand())
			Interface()->UnregisterInterface();
		
		if(Interface()->Parent())
			Interface()->Parent()->StateMachine().DownEvent(Interface());
	}
	
	// there is nothing after established phase and nothing before down phase
	if(next > PPP_ESTABLISHED_PHASE)
		fPhase = PPP_ESTABLISHED_PHASE;
	else if(next < PPP_DOWN_PHASE)
		fPhase = PPP_DOWN_PHASE;
	else
		fPhase = next;
}


// authentication events
void
PPPStateMachine::AuthenticationRequested()
{
	LockerHelper locker(fLock);
	
	fAuthenticationStatus = PPP_AUTHENTICATING;
	free(fAuthenticationName);
	fAuthenticationName = NULL;
}


void
PPPStateMachine::AuthenticationAccepted(const char *name)
{
	LockerHelper locker(fLock);
	
	fAuthenticationStatus = PPP_AUTHENTICATION_SUCCESSFUL;
	free(fAuthenticationName);
	fAuthenticationName = strdup(name);
}


void
PPPStateMachine::AuthenticationDenied(const char *name)
{
	LockerHelper locker(fLock);
	
	fAuthenticationStatus = PPP_AUTHENTICATION_FAILED;
	free(fAuthenticationName);
	fAuthenticationName = strdup(name);
}


const char*
PPPStateMachine::AuthenticationName() const
{
	LockerHelper locker(fLock);
	
	return fAuthenticationName;
}


void
PPPStateMachine::PeerAuthenticationRequested()
{
	LockerHelper locker(fLock);
	
	fPeerAuthenticationStatus = PPP_AUTHENTICATING;
	free(fPeerAuthenticationName);
	fPeerAuthenticationName = NULL;
}


void
PPPStateMachine::PeerAuthenticationAccepted(const char *name)
{
	LockerHelper locker(fLock);
	
	fPeerAuthenticationStatus = PPP_AUTHENTICATION_SUCCESSFUL;
	free(fPeerAuthenticationName);
	fPeerAuthenticationName = strdup(name);
}


void
PPPStateMachine::PeerAuthenticationDenied(const char *name)
{
	LockerHelper locker(fLock);
	
	fPeerAuthenticationStatus = PPP_AUTHENTICATION_FAILED;
	free(fPeerAuthenticationName);
	fPeerAuthenticationName = strdup(name);
}


const char*
PPPStateMachine::PeerAuthenticationName() const
{
	LockerHelper locker(fLock);
	
	return fPeerAuthenticationName;
}


void
PPPStateMachine::UpFailedEvent(PPPInterface *interface)
{
	// TODO:
	// log that an interface did not go up
}


void
PPPStateMachine::UpEvent(PPPInterface *interface)
{
	LockerHelper locker(fLock);
	
	if(Phase() <= PPP_TERMINATION_PHASE || State() != PPP_STARTING_STATE) {
		interface->StateMachine().CloseEvent();
		return;
	}
	
	NewState(PPP_OPENED_STATE);
	if(Phase() == PPP_ESTABLISHMENT_PHASE) {
		NewPhase(PPP_AUTHENTICATION_PHASE);
		locker.UnlockNow();
		ThisLayerUp();
	}
}


void
PPPStateMachine::DownEvent(PPPInterface *interface)
{
	LockerHelper locker(fLock);
	
	// when all children are down we should not be running
	if(Interface()->IsMultilink() && !Interface()->Parent()) {
		uint32 count = 0;
		PPPInterface *child;
		for(int32 i = 0; i < Interface()->CountChildren(); i++) {
			child = Interface()->ChildAt(i);
			
			if(child && child->IsUp())
				++count;
		}
		
		if(count == 0) {
			locker.UnlockNow();
			DownEvent();
		}
	}
}


void
PPPStateMachine::UpFailedEvent(PPPProtocol *protocol)
{
	if(protocol->Flags() & PPP_NOT_IMPORTANT)
		CloseEvent();
}


void
PPPStateMachine::UpEvent(PPPProtocol *protocol)
{
	LockerHelper locker(fLock);
	
	if(Phase() >= PPP_ESTABLISHMENT_PHASE)
		BringHandlersUp();
}


void
PPPStateMachine::DownEvent(PPPProtocol *protocol)
{
}


void
PPPStateMachine::UpFailedEvent(PPPEncapsulator *encapsulator)
{
	if(encapsulator->Flags() & PPP_NOT_IMPORTANT)
		CloseEvent();
}


void
PPPStateMachine::UpEvent(PPPEncapsulator *encapsulator)
{
	LockerHelper locker(fLock);
	
	if(Phase() >= PPP_ESTABLISHMENT_PHASE)
		BringHandlersUp();
}


void
PPPStateMachine::DownEvent(PPPEncapsulator *encapsulator)
{
}


// This is called by the device to tell us that it entered establishment
// phase. We can use Device::Down() to abort establishment until UpEvent()
// is called.
// The return value says if we are waiting for an UpEvent(). If false is
// returned the device should immediately abort its attempt to connect.
bool
PPPStateMachine::TLSNotify()
{
	LockerHelper locker(fLock);
	
	if(State() == PPP_STARTING_STATE) {
			if(Phase() == PPP_DOWN_PHASE)
				NewPhase(PPP_ESTABLISHMENT_PHASE);
					// this says that the device is going up
			return true;
	}
	
	return false;
}


// This is called by the device to tell us that it entered termination phase.
// A Device::Up() should wait until the device went down.
// If false is returned we want to stay connected, though we called
// Device::Down().
bool
PPPStateMachine::TLFNotify()
{
	LockerHelper locker(fLock);
	
	if(Phase() == PPP_ESTABLISHMENT_PHASE) {
		// we may not go down because an OpenEvent indicates that the
		// user wants to connect
		return false;
	}
	
	// from now on no packets may be sent to the device
	NewPhase(PPP_DOWN_PHASE);
	
	return true;
}


void
PPPStateMachine::UpFailedEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_STARTING_STATE:
			// TLSNotify() sets establishment phase
			if(Phase() != PPP_ESTABLISHMENT_PHASE) {
				// there must be a BUG in the device add-on or someone is trying to
				// fool us (UpEvent() is public) as we did not request the device
				// to go up
				IllegalEvent(PPP_UP_FAILED_EVENT);
				NewState(PPP_INITIAL_STATE);
				break;
			}
			
			Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_DEVICE_UP_FAILED,
				NULL, 0);
			
			if(Interface()->Parent())
				Interface()->Parent()->StateMachine().UpFailedEvent(Interface());
		break;
		
		default:
			IllegalEvent(PPP_UP_FAILED_EVENT);
	}
}


void
PPPStateMachine::UpEvent()
{
	// This call is public, thus, it might not only be called by the device.
	// We must recognize these attempts to fool us and handle them correctly.
	
	LockerHelper locker(fLock);
	
	if(!Interface()->Device() || !Interface()->Device->IsUp())
		return;
			// it is not our device that went up...
	
	switch(State()) {
		case PPP_INITIAL_STATE:
			if(Mode() != PPP_SERVER_MODE
				|| Phase() != PPP_ESTABLISHMENT_PHASE) {
				// we are a client or we do not listen for an incoming
				// connection, so this is an illegal event
				IllegalEvent(PPP_UP_EVENT);
				NewState(PPP_CLOSED_STATE);
				locker.UnlockNow();
				ThisLayerFinished();
				
				return;
			}
			
			// TODO: handle server-up!
			NewState(PPP_REQ_SENT_STATE);
			InitializeRestartCount();
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		case PPP_STARTING_STATE:
			// we must have called TLS() which sets establishment phase
			if(Phase() != PPP_ESTABLISHMENT_PHASE) {
				// there must be a BUG in the device add-on or someone is trying to
				// fool us (UpEvent() is public) as we did not request the device
				// to go up
				IllegalEvent(PPP_UP_EVENT);
				NewState(PPP_CLOSED_STATE);
				locker.UnlockNow();
				ThisLayerFinished();
				break;
			}
			
			NewState(PPP_REQ_SENT_STATE);
			InitializeRestartCount();
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		default:
			IllegalEvent(PPP_UP_EVENT);
	}
}


void
PPPStateMachine::DownEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_CLOSED_STATE:
		case PPP_CLOSING_STATE:
			NewState(PPP_INITIAL_STATE);
		break;
		
		case PPP_STOPPED_STATE:
			// The RFC says we should reconnect, but our implementation
			// will only do this if auto-redial is enabled (only clients).
			NewState(PPP_STARTING_STATE);
		break;
		
		case PPP_STOPPING_STATE:
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
		case PPP_OPENED_STATE:
			NewState(PPP_STARTING_STATE);
		break;
		
		default:
			IllegalEvent(PPP_DOWN_EVENT);
	}
	
	// TODO:
	// DownProtocols();
	// DownEncapsulators();
	// ResetOptionHandlers();
	
	NewPhase(PPP_DOWN_PHASE);
	
	// maybe we need to redial
	if(State() == PPP_STARTING_STATE) {
		if(fAuthentiactionStatus == PPP_AUTHENTICATION_FAILED
				|| fAuthenticationStatus == PPP_AUTHENTICATING
				|| fPeerAuthenticationStatus == PPP_AUTHENTICATION_FAILED
				|| fPeerAuthenticationStatus == PPP_AUTHENTICATING)
			Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_AUTHENTICATION_FAILED,
				NULL, 0);
		else
			Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_CONNECTION_LOST,
				NULL, 0);
		
		if(Interface()->Parent())
			Interface()->Parent()->StateMachine().UpFailedEvent(Interface());
		
		NewState(PPP_INITIAL_STATE);
		
		if(Interface()->DoesAutoRedial()) {
			// TODO:
			// redial if we have been connected
			// problem: if we are reconfiguring we should redial, too
//			if(oldState == PPP_OPENED_STATE)
//				Interface()->Redial();
		} else if(!Interface()->DoesDialOnDemand())
			Interface()->Delete();
	} else {
		Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_DOWN_SUCCESSFUL, NULL, 0);
		
		if(!Interface()->DoesDialOnDemand())
			Interface()->Delete();
	}
}


// private events
void
PPPStateMachine::OpenEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
			if(!Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_GOING_UP, NULL, 0))
				return;
			
			if(Interface()->Mode() == PPP_SERVER_MODE) {
				NewPhase(PPP_ESTABLISHMENT_PHASE);
				
				if(Interface()->Device())
					Interface()->Device()->Listen();
			} else
				NewState(PPP_STARTING_STATE);
			
			if(Interface()->IsMultilink() && !Interface()->Parent()) {
				NewPhase(PPP_ESTABLISHMENT_PHASE);
				for(int32 i = 0; i < Interface()->CountChildren(); i++)
					Interface()->ChildAt(i)->StateMachine().OpenEvent();
			} else {
				locker.UnlockNow();
				ThisLayerStarted();
			}
		break;
		
		case PPP_CLOSED_STATE:
			if(Phase() == PPP_DOWN_PHASE) {
				// the device is already going down
				return;
			}
			
			NewState(PPP_REQ_SENT_STATE);
			NewPhase(PPP_ESTABLISHMENT_PHASE);
			InitializeRestartCount();
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		case PPP_CLOSING_STATE:
			NewState(PPP_STOPPING_STATE);
	}
}


void
PPPStateMachine::CloseEvent()
{
	LockerHelper locker(fLock);
	
	if(Interface()->IsMultilink() && !Interface()->Parent()) {
		NewState(PPP_INITIAL_STATE);
		
		if(Phase() != PPP_DOWN_PHASE)
			NewPhase(PPP_TERMINATION_PHASE);
		
		ThisLayerDown();
		
		for(int32 i = 0; i < Interface()->CountChildren(); i++)
			Interface()->ChildAt(i)->StateMachine().CloseEvent();
		
		return;
	}
	
	switch(State()) {
		case PPP_OPENED_STATE:
			ThisLayerDown();
		
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
			NewState(PPP_CLOSING_STATE);
			NewPhase(PPP_TERMINATION_PHASE);
			InitializeRestartCount();
			locker.UnlockNow();
			SendTerminateRequest();
		break;
		
		case PPP_STARTING_STATE:
			NewState(PPP_INITIAL_STATE);
			
			// TLSNotify() will know that we were faster because we
			// are in PPP_INITIAL_STATE now
			if(Phase() == PPP_ESTABLISHMENT_PHASE && Interface()->Parent()) {
				// the device is already up
				NewPhase(PPP_DOWN_PHASE);
					// this says the following DownEvent() was not caused by
					// a connection fault
				locker.UnlockNow();
				ThisLayerFinished();
			}
		break;
		
		case PPP_STOPPING_STATE:
			NewState(PPP_CLOSING_STATE);
		break;
		
		case PPP_STOPPED_STATE:
			NewState(PPP_STOPPED_STATE);
		break;
	}
}


// timeout (restart counters are > 0)
void
PPPStateMachine::TOGoodEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_CLOSING_STATE:
		case PPP_STOPPING_STATE:
			locker.UnlockNow();
			SendTerminateRequest();
		break;
		
		case PPP_ACK_RCVD_STATE:
			NewState(PPP_REQ_SENT_STATE);
		
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_SENT_STATE:
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		default:
			IllegalEvent(PPP_TO_GOOD_EVENT);
	}
}


// timeout (restart counters are <= 0)
void
PPPStateMachine::TOBadEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_CLOSING_STATE:
			NewState(PPP_CLOSED_STATE);
			NewPhase(PPP_TERMINATION_PHASE);
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		case PPP_STOPPING_STATE:
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
			NewState(PPP_STOPPED_STATE);
			NewPhase(PPP_TERMINATION_PHASE);
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		default:
			IllegalEvent(PPP_TO_BAD_EVENT);
	}
}


// receive configure request (acceptable request)
void
PPPStateMachine::RCRGoodEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RCR_GOOD_EVENT);
		break;
		
		case PPP_CLOSED_STATE:
			locker.UnlockNow();
			SendTerminateAck();
		break;
		
		case PPP_STOPPED_STATE:
			// irc,scr,sca/8
			// XXX: should we do nothing and wait for DownEvent()?
		break;
		
		case PPP_REQ_SENT_STATE:
			NewState(PPP_ACK_SENT_STATE);
		
		case PPP_ACK_SENT_STATE:
			locker.UnlockNow();
			SendConfigureAck(packet);
		break;
		
		case PPP_ACK_RCVD_STATE:
			NewState(PPP_OPENED_STATE);
			locker.UnlockNow();
			SendConfigureAck(packet);
			ThisLayerUp();
		break;
		
		case PPP_OPENED_STATE:
			// tld,scr,sca/8
			NewState(PPP_ACK_SENT_STATE);
			NewPhase(PPP_ESTABLISHMENT_PHASE);
				// tell handlers that we are reconfiguring
			locker.UnlockNow();
			ThisLayerDown();
			SendConfigureRequest();
			SendConfigureAck(packet);
		break;
	}
}


// receive configure request (unacceptable request)
void
PPPStateMachine::RCRBadEvent(mbuf *nak, mbuf *reject)
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RCR_BAD_EVENT);
		break;
		
		case PPP_CLOSED_STATE:
			locker.UnlockNow();
			SendTerminateAck();
		break;
		
		case PPP_STOPPED_STATE:
			// irc,scr,scn/6
			// XXX: should we do nothing and wait for DownEvent()?
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_REQ_SENT_STATE);
			NewPhase(PPP_ESTABLISHMENT_PHASE);
				// tell handlers that we are reconfiguring
			locker.UnlockNow();
			ThisLayerDown();
			SendConfigureRequest();
		
		case PPP_ACK_SENT_STATE:
			if(State() == PPP_ACK_SENT_STATE)
				NewState(PPP_REQ_SENT_STATE);
					// OPENED_STATE might have set this already
		
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
			locker.UnlockNow();
			if(!nak && ntohs(mtod(nak, lcp_packet*)->length) > 3)
				SendConfigureNak(nak);
			else if(!reject && ntohs(mtod(reject, lcp_packet*)->length) > 3)
				SendConfigureNak(reject);
		break;
	}
}


// receive configure ack
void
PPPStateMachine::RCAEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	if(fRequestID != mtod(packet, lcp_packet*)->id) {
		// this packet is not a reply to our request
		
		// TODO:
		// log this event
		return;
	}
	
	PPPOptionHandler *handler;
	PPPConfigurePacket ack(packet);
	
	for(int32 i = 0; i < LCP().CountOptionHandlers(); i++) {
		handler = LCP().OptionHandlerAt(i);
		handler->ParseAck(ack);
	}
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RCA_EVENT);
		break;
		
		case PPP_CLOSED_STATE:
		case PPP_STOPPED_STATE:
			locker.UnlockNow();
			SendTerminateAck();
		break;
		
		case PPP_REQ_SENT_STATE:
			NewState(PPP_ACK_RCVD_STATE);
			InitializeRestartCount();
		break;
		
		case PPP_ACK_RCVD_STATE:
			NewState(PPP_REQ_SENT_STATE);
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		case PPP_ACK_SENT_STATE:
			NewState(PPP_OPENED_STATE);
			InitializeRestartCount();
			locker.UnlockNow();
			ThisLayerUp();
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_REQ_SENT_STATE);
			NewPhase(PPP_ESTABLISHMENT_PHASE);
				// tell handlers that we are reconfiguring
			locker.UnlockNow();
			ThisLayerDown();
			SendConfigureRequest();
		break;
	}
}


// receive configure nak/reject
void
PPPStateMachine::RCNEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	if(fRequestID != mtod(packet, lcp_packet*)->id) {
		// this packet is not a reply to our request
		
		// TODO:
		// log this event
		return;
	}
	
	PPPOptionHandler *handler;
	PPPConfigurePacket nak_reject(packet);
	
	for(int32 i = 0; i < LCP().CountOptionHandlers(); i++) {
		handler = LCP().OptionHandlerAt(i);
		
		if(nak_reject.Code() == PPP_CONFIGURE_NAK)
			handler->ParseNak(nak_reject);
		else if(nak_reject.Code() == PPP_CONFIGURE_REJECT)
			handler->ParseReject(nak_reject);
	}
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RCN_EVENT);
		break;
		
		case PPP_CLOSED_STATE:
		case PPP_STOPPED_STATE:
			locker.UnlockNow();
			SendTermintateAck(packet);
		break;
		
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_SENT_STATE:
			InitializeRestartCount();
		
		case PPP_ACK_RCVD_STATE:
			if(State() == PPP_ACK_RCVD_STATE)
				NewState(PPP_REQ_SENT_STATE);
			locker.UnlockNow();
			SendConfigureRequest();
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_REQ_SENT_STATE);
			NewPhase(PPP_ESTABLISHMENT_PHASE);
				// tell handlers that we are reconfiguring
			locker.UnlockNow();
			ThisLayerDown();
			SendConfigureRequest();
		break;
	}
}


// receive terminate request
void
PPPStateMachine::RTREvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	// we should not use the same ID as the peer
	if(fID == mtod(packet, lcp_packet*)->id)
		fID -= 128;
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RTR_EVENT);
		break;
		
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
			NewState(PPP_REQ_SENT_STATE);
			NewPhase(PPP_TERMINATION_PHASE);
				// tell handlers that we are terminating
			locker.UnlockNow();
			SendTerminateAck(packet);
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_STOPPING_STATE);
			NewPhase(PPP_TERMINATION_PHASE);
				// tell handlers that we are terminating
			ZeroRestartCount();
			locker.UnlockNow();
			ThisLayerDown();
			SendTerminateAck(packet);
		break;
		
		default:
			NewPhase(PPP_TERMINATION_PHASE);
				// tell handlers that we are terminating
			locker.UnlockNow();
			SendTerminateAck(packet);
	}
}


// receive terminate ack
void
PPPStateMachine::RTAEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	if(fTerminateID != mtod(packet, lcp_packet*)->id) {
		// this packet is not a reply to our request
		
		// TODO:
		// log this event
		return;
	}
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RTA_EVENT);
		break;
		
		case PPP_CLOSING_STATE:
			NewState(PPP_CLOSED_STATE);
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		case PPP_CLOSING_STATE:
			NewState(PPP_STOPPED_STATE);
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		case PPP_ACK_RCVD_STATE:
			NewState(REQ_SENT_STATE);
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_REQ_SENT_STATE);
			locker.UnlockNow();
			ThisLayerDown();
			SendConfigureRequest();
		break;
	}
}


// receive unknown code
void
PPPStateMachine::RUCEvent(mbuf *packet, uint16 protocol, uint8 type = PPP_PROTOCOL_REJECT)
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RUC_EVENT);
		break;
		
		default:
			locker.UnlockNow();
			SendCodeReject(packet, protocol, type);
	}
}


// receive code/protocol reject (acceptable such as IPX reject)
void
PPPStateMachine::RXJGoodEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RXJ_GOOD_EVENT);
		break;
		
		case PPP_ACK_RCVD_STATE:
			NewState(PPP_REQ_SENT_STATE);
		break;
	}
}


// receive code/protocol reject (catastrophic such as LCP reject)
void
PPPStateMachine::RXJBadEvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RJX_BAD_EVENT);
		break;
		
		case PPP_CLOSING_STATE:
			NewState(PPP_CLOSED_STATE);
		
		case PPP_CLOSED_STATE:
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		case PPP_STOPPING_STATE:
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
			NewState(PPP_STOPPED_STATE);
		
		case PPP_STOPPED_STATE:
			locker.UnlockNow();
			ThisLayerFinished();
		break;
		
		case PPP_OPENED_STATE:
			NewState(PPP_STOPPING_STATE);
			InitializeRestartCount();
			locker.UnlockNow();
			ThisLayerDown();
			SendTerminateRequest();
		break;
	}
}


// receive echo request/reply, discard request
void
PPPStateMachine::RXREvent(mbuf *packet)
{
	LockerHelper locker(fLock);
	
	lcp_packet *echo = mtod(mbuf, lcp_packet*);
	
	switch(State()) {
		case PPP_INITIAL_STATE:
		case PPP_STARTING_STATE:
			IllegalEvent(PPP_RXR_EVENT);
		break;
		
		case PPP_OPENED_STATE:
			if(echo->code == PPP_ECHO_REQUEST)
				SendEchoReply(packet);
		break;
	}
}


// general events (for Good/Bad events)
void
PPPStateMachine::TimerEvent()
{
	LockerHelper locker(fLock);
	
	switch(State()) {
		case PPP_CLOSING_STATE:
		case PPP_STOPPTING_STATE:
			if(fTerminateCounter <= 0)
				TOBadEvent();
			else
				TOGoodEvent();
		break;
		
		case PPP_REQ_SENT_STATE:
		case PPP_ACK_RCVD_STATE:
		case PPP_ACK_SENT_STATE:
			if(fConfigureCounter <= 0)
				TOBadEvent();
			else
				TOGoodEvent();
		break;
	}
	
}


// ReceiveConfigureRequest
// Here we get a configure-request packet from LCP and aks all OptionHandlers
// if its values are acceptable. From here we call our Good/Bad counterparts.
void
PPPStateMachine::RCREvent(mbuf *packet)
{
	PPPConfigurePacket request(packet), nak(PPP_CONFIGURE_NAK),
		reject(PPP_CONFIGURE_REJECT);
	PPPOptionHandler *handler;
	
	// we should not use the same id as the peer
	if(fID == mtod(packet, lcp_packet*)->id)
		fID -= 128;
	
	nak.SetID(request.ID());
	reject.SetID(request.ID());
	
	// each handler should add unacceptable values for each item
	bool handled;
	int32 error;
	for(int32 item = 0; item <= request.CountItems(); item++) {
		// if we sent too many naks we should not append additional values
		if(fNakCounter == 0 && item == request.CountItems())
			break;
		
		handled = false;
		
		for(int32 index = 0; index < LCP().CountOptionHandlers();
			index++) {
			handler = LCP().OptionHandlerAt(i);
			error = handler->ParseRequest(&request, item, &nak, &reject);
			if(error == PPP_UNHANLED)
				continue;
			else if(error == B_ERROR) {
				// the request contains a value that has been sent more than
				// once or the value is corrupted
				CloseEvent();
				return;
			} else if(error = B_OK)
				handled = true;
		}
		
		if(!handled && item < request.CountItems()) {
			// unhandled items should be added to reject
			reject.AddItem(request.ItemAt(item));
		}
	}
	
	if(nak.CountItems() > 0)
		RCRBadEvent(nak.ToMbuf(), NULL);
	else if(reject.CountItmes() > 0)
		RCRBadEvent(NULL, reject.ToMbuf());
	else
		RCRGoodEvent(packet);
}


// ReceiveCodeReject
// LCP received a code/protocol-reject packet and we look if it is acceptable.
// From here we call our Good/Bad counterparts.
void
PPPStateMachine::RXJEvent(mbuf *packet)
{
	lcp_packet *reject mtod(packet, lcp_packet*);
	
	if(reject->code == PPP_CODE_REJECT) {
		// we only have basic codes, so there must be something wrong
		if(Interface()->IsMultilink() && !Interface()->Parent())
			CloseEvent();
		else
			RXJBadEvent(packet);
	} else if(reject->code == PPP_PROTOCOL_REJECT) {
		// disable all handlers for rejected protocol type
		uint16 rejected = *((uint16*) reject->data);
			// rejected protocol number
		
		if(rejected == PPP_LCP_PROTOCOL) {
			// LCP must not be rejected!
			RXJBadEvent(packet);
			return;
		}
		
		int32 index;
		PPPProtocol *protocol_handler;
		PPPEncapsulator *encapsulator_handler = Interface()->FirstEncapsulator();
		
		for(index = 0; index < Interface()->CountProtocols(); index++) {
			protocol_handler = Interface()->ProtocolAt(index);
			if(protocol_handler && protocol_handler->Protocol() == rejected)
				protocol_handler->SetEnabled(false);
					// disable protocol
		}
		
		for(; encapsulator_handler;
				encapsulator_handler = encapsulator_handler->Next()) {
			if(encapsulator_handler->Protocol() == rejected)
				encapsulator_handler->SetEnabled(false);
					// disable encapsulator
		}
		
		RXJGoodEvent(packet);
		
		// notify parent, too
		if(Interface()->Parent())
			Interface()->Parent()->StateMachine().RXJEvent(packet);
	}
}


// actions (all private)
void
PPPStateMachine::IllegalEvent(PPP_EVENT event)
{
	// TODO:
	// update error statistics
}


void
PPPStateMachine::ThisLayerUp()
{
	LockerHelper locker(fLock);
	
	// We begin with authentication phase and wait until each phase is done.
	// We stop when we reach established phase.
	
	// Do not forget to check if we are going down.
	if(Phase() != PPP_ESTABLISHMENT_PHASE)
		return;
	
	NewPhase(PPP_AUTHENTICATION_PHASE);
	
	locker.UnlockNow();
	
	BringHandlersUp();
}


void
PPPStateMachine::ThisLayerDown()
{
	// TODO:
	// DownProtocols();
	// DownEncapsulators();
	
	// PPPProtocol/Encapsulator::Down() should block if needed.
}


void
PPPStateMachine::ThisLayerStarted()
{
	if(Interface()->Device())
		Interface()->Device()->Up();
}


void
PPPStateMachine::ThisLayerFinished()
{
	if(Interface()->Device())
		Interface()->Device()->Down();
}


void
PPPStateMachine::InitializeRestartCount()
{
	fConfigureCounter = fMaxConfigure;
	fTerminateCounter = fMaxTerminate;
	fNakCounter = fMaxNak;
	
	// TODO:
	// start timer
}


void
PPPStateMachine::ZeroRestartCount()
{
	fConfigureCounter = 0;
	fTerminateCounter = 0;
	fNakCounter = 0;
	
	// TODO:
	// start timer
}


void
PPPStateMachine::SendConfigureRequest()
{
	--fConfigureCounter;
	
	PPPConfigurePacket request(PPP_CONFIGURE_REQUEST);
	request.SetID(NextID());
	fConfigureID = request.ID();
	
	for(int32 i = 0; i < LCP().CountOptionHandlers(); i++) {
		// add all items
		if(!LCP().OptionHandlerAt(i)->AddToRequest(&request)) {
			CloseEvent();
			return;
		}
	}
	
	LCP().Send(request.ToMbuf());
	
	fTimeout = system_time();
}


void
PPPStateMachine::SendConfigureAck(mbuf *packet)
{
	mtod(packet, lcp_packet*)->code = PPP_CONFIGURE_ACK;
	PPPConfigurePacket ack(packet);
	
	for(int32 i = 0; i < LCP().CountOptionHandlers; i++)
		LCP().OptionHandlerAt(i)->SendingAck(&ack);
	
	LCP().Send(packet);
}


void
PPPStateMachine::SendConfigureNak(mbuf *packet)
{
	lcp_packet *nak = mtod(packet, lcp_packet*);
	if(nak->code == PPP_CONFIGURE_NAK) {
		if(fNakCounter == 0) {
			// We sent enough naks. Let's try a reject.
			nak->code = PPP_CONFIGURE_REJECT;
		} else
			--fNakCounter;
	}
	
	LCP().Send(packet);
}


void
PPPStateMachine::SendTerminateRequest()
{
	mbuf *m = m_gethdr(MT_DATA);
	if(!request)
		return;
	
	--fTerminateCounter;
	
	// reserve some space for other protocols
	m->m_data += PPP_PROTOCOL_OVERHEAD;
	if(LCP().Encapsulator())
		m->m_data += LCP().Encapsulator()->Overhead();
	if(Interface()->Device())
		m->m_data += Interface()->Device()->Overhead();
	
	lcp_packet *request = mtod(m, lcp_packet*);
	request->code = PPP_TERMINATE_REQUEST;
	request->id = fTerminateID = NextID();
	request->length = htons(4);
	
	LCP().Send(m);
}


void
PPPStateMachine::SendTerminateAck(mbuf *request)
{
	lcp_packet *ack = mtod(request, lcp_packet*);
	reply->code = PPP_TERMINATE_ACK;
		// the request becomes an ack
	
	LCP().Send(request);
}


void
PPPStateMachine::SendCodeReject(mbuf *packet, uint16 protocol, uint8 type)
{
	// TODO:
	// add the packet to the reject and truncate it if needed
}


void
PPPStateMachine::SendEchoReply(mbuf *request)
{
	lcp_packet *reply = mtod(request, lcp_packet*);
	reply->code = PPP_ECHO_REPLY;
		// the request becomes a reply
	
	LCP().Send(request);
}


// methods for bringing protocols/encapsulators up
void
PPPStateMachine::BringHandlersUp()
{
	while(Phase() <= PPP_ESTABLISHED_PHASE && Phase() >= PPP_AUTHENTICATION_PHASE) {
		if(BringPhaseUp() > 0)
			break;
		
		LockerHelper locker(fLock);
		
		if(Phase() < PPP_AUTHENTICATION_PHASE)
			return;
		else if(Phase() == PPP_ESTABLISHED_PHASE) {
			if(Interface()->Parent())
				Interface()->Parent()->StateMachine().UpEvent(Interface());
			
			if(Interface()->Ifnet())
				Interface()->Ifnet()->if_flags |= IFF_RUNNING;
			
			Interface()->Report(PPP_CONNECTION_REPORT, PPP_REPORT_UP_SUCCESSFUL, NULL, 0);
		} else
			NewPhase(Phase() + 1);
	}
}


// this returns the number of handlers waiting to go up
uint32
PPPStateMachine::BringPhaseUp()
{
	LockerHelper locker(fLock);
	
	if(Phase() < PPP_AUTHENTICATION_PHASE)
		return 0;
	
	uint32 count = 0;
	PPPProtocol *protocol_handler;
	PPPEncapsulator *encapsulator_handler = Interface()->FirstEncapsulator();
	
	for(int32 index = 0; index < Interface()->CountProtocols(); index++) {
		protocol_handler = Interface()->ProtocolAt(index);
		if(protocol_handler && protocol_handler->Phase() == Phase()) {
			if(protocol_handler->IsUpRequested()) {
				++count;
				protocol_handler->Up();
			} else if(protocol_handler->IsGoingUp())
				++count;
		}
	}
	
	for(; encapsulator_handler;
			encapsulator_handler = encapsulator_handler->Next()) {
		if(encapsulator_handler && encapsulator_handler->Phase() == Phase()) {
			if(encapsulator_handler->IsUpRequested()) {
				++count;
				encapsulator_handler->Up();
			} else if(encapsulator_handler->IsGoingUp())
				++count;
		}
	}
	
	return count;
}
