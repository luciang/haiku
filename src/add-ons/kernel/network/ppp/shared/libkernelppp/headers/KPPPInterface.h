//-----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003-2004 Waldemar Kornewald, Waldemar.Kornewald@web.de
//-----------------------------------------------------------------------

#ifndef _K_PPP_INTERFACE__H
#define _K_PPP_INTERFACE__H

#include <driver_settings.h>

#ifndef _K_PPP_DEFS__H
#include <KPPPDefs.h>
#endif

#ifndef _K_PPP_LCP__H
#include <KPPPLCP.h>
#endif

#ifndef _K_PPP_PROFILE__H
#include <KPPPProfile.h>
#endif

#ifndef _K_PPP_REPORT_MANAGER__H
#include <KPPPReportManager.h>
#endif

#ifndef _K_PPP_STATE_MACHINE__H
#include <KPPPStateMachine.h>
#endif

#include <TemplateList.h>

class KPPPDevice;
class KPPPProtocol;
class KPPPOptionHandler;

struct ppp_interface_module_info;
struct ppp_module_info;
struct ppp_interface_entry;


class KPPPInterface : public KPPPLayer {
		friend class PPPManager;
		friend class KPPPStateMachine;
		friend class KPPPInterfaceAccess;

	private:
		//!	Copies are not allowed.
		KPPPInterface(const KPPPInterface& copy);
		KPPPInterface& operator= (const KPPPInterface& copy);
		
		// only PPPManager may construct us!
		KPPPInterface(const char *name, ppp_interface_entry *entry,
			ppp_interface_id ID, const driver_settings *settings,
			const driver_settings *profile, KPPPInterface *parent = NULL);
		~KPPPInterface();

	public:
		void Delete();
		
		virtual status_t InitCheck() const;
		
		//!	Returns this interface's unique identifier.
		ppp_interface_id ID() const
			{ return fID; }
		
		//!	Returns interface's settings.
		driver_settings* Settings() const
			{ return fSettings; }
		
		//!	Returns the KPPPStateMachine of this interface.
		KPPPStateMachine& StateMachine()
			{ return fStateMachine; }
		//!	Returns the KPPPLCP protocol of this interface.
		KPPPLCP& LCP()
			{ return fLCP; }
		//!	Returns interface's profile object.
		KPPPProfile& Profile()
			{ return fProfile; }
		
		//!	Returns the interfac's ifnet structure that is exported to the netstack.
		struct ifnet *Ifnet() const
			{ return fIfnet; }
		
		//!	Delay in miliseconds between a dial retry.
		uint32 DialRetryDelay() const
			{ return fDialRetryDelay; }
		//!	Delay in miliseconds to wait until redialing.
		uint32 RedialDelay() const
			{ return fRedialDelay; }
		
		//!	Used for reporting that a packet was send/received (updates idle time).
		void UpdateIdleSince()
			{ fUpdateIdleSince = true; }
		//!	Returns the time in seconds when the last packet was received.
		uint32 IdleSince() const
			{ return fIdleSince; }
		//!	If no packets were received this number of seconds we will disconnect.
		uint32 DisconnectAfterIdleSince() const
			{ return fDisconnectAfterIdleSince; }
		
		bool SetMRU(uint32 MRU);
		//!	This is the smallest MRU that we and the peer have.
		uint32 MRU() const
			{ return fMRU; }
		//!	Sets interface's maximum transfer unit (will set ifnet value accordingly).
		bool SetInterfaceMTU(uint32 interfaceMTU)
			{ return SetMRU(interfaceMTU - fHeaderLength); }
		//!	This is the MRU including protocol overhead.
		uint32 InterfaceMTU() const
			{ return fInterfaceMTU; }
		//!	Includes the length of all device and encapsulator headers.
		uint32 PacketOverhead() const;
		
		virtual status_t Control(uint32 op, void *data, size_t length);
		
		bool SetDevice(KPPPDevice *device);
		//!	Returns interface's transport device.
		KPPPDevice *Device() const
			{ return fDevice; }
		
		bool AddProtocol(KPPPProtocol *protocol);
		bool RemoveProtocol(KPPPProtocol *protocol);
		int32 CountProtocols() const;
		KPPPProtocol *ProtocolAt(int32 index) const;
		//!	Returns first protocol in chain.
		KPPPProtocol *FirstProtocol() const
			{ return fFirstProtocol; }
		KPPPProtocol *ProtocolFor(uint16 protocolNumber,
			KPPPProtocol *start = NULL) const;
		
		// multilink methods
		bool AddChild(KPPPInterface *child);
		bool RemoveChild(KPPPInterface *child);
		//!	Returns the number of child interfaces that this interface has.
		int32 CountChildren() const
			{ return fChildren.CountItems(); }
		KPPPInterface *ChildAt(int32 index) const;
		//!	Returns this interface's parent.
		KPPPInterface *Parent() const
			{ return fParent; }
		//!	Returns whether we are a multilink-interface.
		bool IsMultilink() const
			{ return fIsMultilink; }
		
		void SetAutoRedial(bool autoRedial = true);
		//!	Returns whether this interface redials automatically.
		bool DoesAutoRedial() const
			{ return fAutoRedial; }
		
		void SetDialOnDemand(bool dialOnDemand = true);
		//!	Returns whether dial-on-demand (auto-dial) is enabled.
		bool DoesDialOnDemand() const
			{ return fDialOnDemand; }
		
		//!	Clients are in \c PPP_CLIENT_MODE and servers are in \c PPP_SERVER_MODE.
		ppp_mode Mode() const
			{ return fMode; }
		//!	Current state of the state machine (see enum: \c ppp_state).
		ppp_state State() const
			{ return fStateMachine.State(); }
		//!	Current phase of the state machine (see enum: \c ppp_phase).
		ppp_phase Phase() const
			{ return fStateMachine.Phase(); }
		
		// Protocol-Field-Compression
		bool SetPFCOptions(uint8 pfcOptions);
		//!	PFC option flags as defined in enum: \c ppp_pfc_options.
		uint8 PFCOptions() const
			{ return fPFCOptions; }
		/*!	\brief Local PFC state
			
			Values defined in \c ppp_pfc_state.\n
			The local PFC state says if we accepted a request from the peer
			i.e.: we may use PFC in outgoing packets.
		*/
		ppp_pfc_state LocalPFCState() const
			{ return fLocalPFCState; }
		/*!	\brief Peer PFC state
			
			Values defined in \c ppp_pfc_state.\n
			The peer PFC state says if the peer accepted a request us
			i.e.: the peer might send PFC-compressed packets to us
		*/
		ppp_pfc_state PeerPFCState() const
			{ return fPeerPFCState; }
		//!	Shortcut for check if local state is \c PPP_PFC_ACCEPTED.
		bool UseLocalPFC() const
			{ return LocalPFCState() == PPP_PFC_ACCEPTED; }
		
		virtual bool Up();
			// in server mode Up() listens for an incoming connection
		virtual bool Down();
		bool IsUp() const;
		
		//!	Returns interface's report manager.
		KPPPReportManager& ReportManager()
			{ return fReportManager; }
		//!	Shortcut to KPPPReportManager::Report() of this interface's report manager.
		bool Report(ppp_report_type type, int32 code, void *data, int32 length)
			{ return ReportManager().Report(type, code, data, length); }
			// returns false if reply was bad (or an error occured)
		
		bool LoadModules(driver_settings *settings,
			int32 start, int32 count);
		bool LoadModule(const char *name, driver_parameter *parameter,
			ppp_module_key_type type);
		
		virtual bool IsAllowedToSend() const;
			// always returns true
		
		virtual status_t Send(struct mbuf *packet, uint16 protocolNumber);
			// sends the packet to the next handler (normally the device)
		virtual status_t Receive(struct mbuf *packet, uint16 protocolNumber);
		
		status_t ReceiveFromDevice(struct mbuf *packet);
			// This must not block KPPPDevice::Send()!
		
		void Pulse();
			// this manages all timeouts, etc.

	private:
		bool RegisterInterface();
			// adds us to the manager module and
			// saves the returned ifnet structure
		bool UnregisterInterface();
		
		void UpdateProfile();
		
		status_t StackControl(uint32 op, void *data);
			// stack routes ioctls to interface
		status_t StackControlEachHandler(uint32 op, void *data);
			// this calls StackControl() with the given parameters for each handler
		
		void CalculateInterfaceMTU();
		void CalculateBaudRate();
		
		// these two methods are used by the open/close_event_threads
		void CallOpenEvent()
			{ StateMachine().OpenEvent(); }
		void CallCloseEvent()
			{ StateMachine().CloseEvent(); }
		
		void Redial(uint32 delay);
		
		// multilink methods
		//!	Set the parent of this interface.
		void SetParent(KPPPInterface *parent)
			{ fParent = parent; }

	private:
		ppp_interface_id fID;
			// the manager assigns an ID to every interface
		driver_settings *fSettings;
		struct ifnet *fIfnet;
		
		thread_id fUpThread, fOpenEventThread, fCloseEventThread;
		
		thread_id fRedialThread;
		uint32 fDialRetry, fDialRetriesLimit;
		uint32 fDialRetryDelay, fRedialDelay;
		
		ppp_interface_module_info *fManager;
		
		uint32 fIdleSince, fDisconnectAfterIdleSince;
		bool fUpdateIdleSince;
		uint32 fMRU, fInterfaceMTU, fHeaderLength;
		
		KPPPInterface *fParent;
		TemplateList<KPPPInterface*> fChildren;
		bool fIsMultilink;
		
		bool fAutoRedial, fDialOnDemand;
		
		ppp_mode fMode;
		ppp_pfc_state fLocalPFCState, fPeerPFCState;
		uint8 fPFCOptions;
		
		KPPPDevice *fDevice;
		KPPPProtocol *fFirstProtocol;
		TemplateList<char*> fModules;
		
		KPPPStateMachine fStateMachine;
		KPPPLCP fLCP;
		KPPPProfile fProfile;
		KPPPReportManager fReportManager;
		BLocker& fLock;
		int32 fDeleteCounter;
};


#endif
