//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Waldemar Kornewald, Waldemar.Kornewald@web.de
//---------------------------------------------------------------------

#ifndef _K_PPP_LCP__H
#define _K_PPP_LCP__H

#include <List.h>

#ifndef _K_PPP_PROTOCOL__H
#include <KPPPProtocol.h>
#endif

#ifndef _K_PPP_INTERFACE__H
#include <KPPPInterface.h>
#endif

#ifndef _K_PPP_STATE_MACHINE__H
#include <KPPPStateMachine.h>
#endif

class PPPEncapsulator;
class PPPOptionHandler;


typedef struct ppp_lcp_packet {
	uint8 code;
	uint8 id;
	uint16 length;
	int8 data[0];
} ppp_lcp_packet;


class PPPLCP : public PPPProtocol {
		friend class PPPInterface;

	private:
		// may only be constructed/destructed by PPPInterface
		PPPLCP(PPPInterface& interface);
		virtual ~PPPLCP();
		
		// copies are not allowed!
		PPPLCP(const PPPLCP& copy);
		PPPLCP& operator= (const PPPLCP& copy);

	public:
		PPPStateMachine& StateMachine() const
			{ return fStateMachine; }
		
		bool AddOptionHandler(PPPOptionHandler *handler);
		bool RemoveOptionHandler(PPPOptionHandler *handler);
		int32 CountOptionHandlers() const
			{ return fOptionHandlers.CountItems(); }
		PPPOptionHandler *OptionHandlerAt(int32 index) const;
		
		PPPEncapsulator *Target() const
			{ return fTarget; }
		void SetTarget(PPPEncapsulator *target)
			{ fTarget = target; }
			// if target != all packtes will be passed to the encapsulator
			// instead of the interface/device
		
		uint32 AdditionalOverhead() const;
			// the overhead caused by the target, the device, and the interface
		
		virtual bool Up();
		virtual bool Down();
		
		virtual status_t Send(struct mbuf *packet);
		virtual status_t Receive(struct mbuf *packet, uint16 protocol);
		
		virtual void Pulse();

	private:
		PPPStateMachine& fStateMachine;
		
		List<PPPOptionHandler*> fOptionHandlers;
		
		PPPEncapsulator *fTarget;
};


#endif
