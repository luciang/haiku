/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */

#ifndef _USB_P_
#define _USB_P_

#include "BeOSCompatibility.h"
#include "usbspec_p.h"
#include <lock.h>

//#define TRACE_USB
#ifdef TRACE_USB
#define TRACE(x)		dprintf x
#define TRACE_ERROR(x)	dprintf x
#else
#define TRACE(x)		/* nothing */
#define TRACE_ERROR(x)	dprintf x
#endif


class Hub;
class Stack;
class Device;
class Transfer;
class BusManager;
class Pipe;
class ControlPipe;
class Object;
class PhysicalMemoryAllocator;


struct usb_host_controller_info {
	module_info		info;
	status_t		(*control)(uint32 op, void *data, size_t length);
	status_t		(*add_to)(Stack *stack);
};


struct usb_driver_cookie {
	usb_id							device;
	void							*cookie;
	usb_driver_cookie				*link;
};


struct usb_driver_info {
	const char						*driver_name;
	usb_support_descriptor			*support_descriptors;
	uint32							support_descriptor_count;
	const char						*republish_driver_name;
	usb_notify_hooks				notify_hooks;
	usb_driver_cookie				*cookies;
	usb_driver_info					*link;
};


struct change_item {
	bool							added;
	Device 							*device;
	change_item						*link;
};


struct rescan_item {
	const char						*name;
	rescan_item						*link;
};


typedef enum {
	USB_SPEED_LOWSPEED = 0,
	USB_SPEED_FULLSPEED,
	USB_SPEED_HIGHSPEED,
	USB_SPEED_MAX = USB_SPEED_HIGHSPEED
} usb_speed;


typedef enum {
	USB_CHANGE_CREATED = 0,
	USB_CHANGE_DESTROYED,
	USB_CHANGE_PIPE_POLICY_CHANGED
} usb_change;


#define USB_OBJECT_NONE					0x00000000
#define USB_OBJECT_PIPE					0x00000001
#define USB_OBJECT_CONTROL_PIPE			0x00000002
#define USB_OBJECT_INTERRUPT_PIPE		0x00000004
#define USB_OBJECT_BULK_PIPE			0x00000008
#define USB_OBJECT_ISO_PIPE				0x00000010
#define USB_OBJECT_INTERFACE			0x00000020
#define USB_OBJECT_DEVICE				0x00000040
#define USB_OBJECT_HUB					0x00000080


class Stack {
public:
										Stack();
										~Stack();

		status_t						InitCheck();

		bool							Lock();
		void							Unlock();

		usb_id							GetUSBID(Object *object);
		void							PutUSBID(usb_id id);
		Object							*GetObject(usb_id id);

		void							AddBusManager(BusManager *bus);
		int32							IndexOfBusManager(BusManager *bus);
		BusManager						*BusManagerAt(int32 index);

		status_t						AllocateChunk(void **logicalAddress,
											void **physicalAddress, size_t size);
		status_t						FreeChunk(void *logicalAddress,
											void *physicalAddress, size_t size);

		area_id							AllocateArea(void **logicalAddress,
											void **physicalAddress,
											size_t size, const char *name);

		void							NotifyDeviceChange(Device *device,
											rescan_item **rescanList,
											bool added);
		void							RescanDrivers(rescan_item *rescanItem);

		// USB API
		status_t						RegisterDriver(const char *driverName,
											const usb_support_descriptor *descriptors,
											size_t descriptorCount,
											const char *republishDriverName);

		status_t						InstallNotify(const char *driverName,
											const usb_notify_hooks *hooks);
		status_t						UninstallNotify(const char *driverName);

private:
static	int32							ExploreThread(void *data);

		Vector<BusManager *>			fBusManagers;
		thread_id						fExploreThread;
		bool							fFirstExploreDone;
		bool							fStopThreads;

		mutex							fStackLock;
		mutex							fExploreLock;
		PhysicalMemoryAllocator			*fAllocator;

		uint32							fObjectIndex;
		uint32							fObjectMaxCount;
		Object							**fObjectArray;

		usb_driver_info					*fDriverList;
};


/*
 * This class manages a bus. It is created by the Stack object
 * after a host controller gives positive feedback on whether the hardware
 * is found.
 */
class BusManager {
public:
										BusManager(Stack *stack);
virtual									~BusManager();

virtual	status_t						InitCheck();

		bool							Lock();
		void							Unlock();

		int8							AllocateAddress();
		void							FreeAddress(int8 address);

		Device							*AllocateDevice(Hub *parent,
											uint8 port, usb_speed speed);
		void							FreeDevice(Device *device);

virtual	status_t						Start();
virtual	status_t						Stop();

virtual	status_t						SubmitTransfer(Transfer *transfer);
virtual	status_t						CancelQueuedTransfers(Pipe *pipe,
											bool force);

virtual	status_t						NotifyPipeChange(Pipe *pipe,
											usb_change change);

		Object							*RootObject() { return fRootObject; };

		Hub								*GetRootHub() { return fRootHub; };
		void							SetRootHub(Hub *hub) { fRootHub = hub; };

protected:
		bool							fInitOK;

private:
		ControlPipe 					*_GetDefaultPipe(usb_speed);

		mutex							fLock;

		bool							fDeviceMap[128];
		int8							fDeviceIndex;

		ControlPipe						*fDefaultPipes[USB_SPEED_MAX + 1];
		Hub								*fRootHub;
		Object							*fRootObject;
};


class Object {
public:
										Object(Stack *stack, BusManager *bus);
										Object(Object *parent);
virtual									~Object();

		Object							*Parent() { return fParent; };

		BusManager						*GetBusManager() { return fBusManager; };
		Stack							*GetStack() { return fStack; };

		usb_id							USBID() { return fUSBID; };
virtual	uint32							Type() { return USB_OBJECT_NONE; };

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

private:
		Object							*fParent;
		BusManager						*fBusManager;
		Stack							*fStack;
		usb_id							fUSBID;
};


/*
 * The Pipe class is the communication management between the hardware and
 * the stack. It creates packets, manages these and performs callbacks.
 */
class Pipe : public Object {
public:
enum	pipeDirection 	{ In, Out, Default };

										Pipe(Object *parent);
virtual									~Pipe();

		void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort);

virtual	uint32							Type() { return USB_OBJECT_PIPE; };

		int8							DeviceAddress() { return fDeviceAddress; };
		usb_speed						Speed() { return fSpeed; };
		pipeDirection					Direction() { return fDirection; };
		uint8							EndpointAddress() { return fEndpointAddress; };
		size_t							MaxPacketSize() { return fMaxPacketSize; };
		uint8							Interval() { return fInterval; };

		void							SetHubInfo(int8 address, uint8 port);
		int8							HubAddress() { return fHubAddress; };
		uint8							HubPort() { return fHubPort; };

virtual	bool							DataToggle() { return fDataToggle; };
virtual	void							SetDataToggle(bool toggle) { fDataToggle = toggle; };

		status_t						SubmitTransfer(Transfer *transfer);
		status_t						CancelQueuedTransfers(bool force);

		void							SetControllerCookie(void *cookie) { fControllerCookie = cookie; };
		void							*ControllerCookie() { return fControllerCookie; };

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

private:
		int8							fDeviceAddress;
		uint8							fEndpointAddress;
		pipeDirection					fDirection;
		usb_speed						fSpeed;
		size_t							fMaxPacketSize;
		uint8							fInterval;
		int8							fHubAddress;
		uint8							fHubPort;
		bool							fDataToggle;
		void							*fControllerCookie;
};


class ControlPipe : public Pipe {
public:
										ControlPipe(Object *parent);
virtual									~ControlPipe();

virtual	uint32							Type() { return USB_OBJECT_PIPE | USB_OBJECT_CONTROL_PIPE; };

										// The data toggle is not relevant
										// for control transfers, as they are
										// always enclosed by a setup and
										// status packet. The toggle always
										// starts at 1.
virtual	bool							DataToggle() { return true; };
virtual	void							SetDataToggle(bool toggle) {};

		status_t						SendRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											size_t *actualLength);
static	void							SendRequestCallback(void *cookie,
											status_t status, void *data,
											size_t actualLength);

		status_t						QueueRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);

private:
		sem_id							fNotifySem;
		status_t						fTransferStatus;
		size_t							fActualLength;
};


class InterruptPipe : public Pipe {
public:
										InterruptPipe(Object *parent);

virtual	uint32							Type() { return USB_OBJECT_PIPE | USB_OBJECT_INTERRUPT_PIPE; };

		status_t						QueueInterrupt(void *data,
											size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);
};


class BulkPipe : public Pipe {
public:
										BulkPipe(Object *parent);

virtual	uint32							Type() { return USB_OBJECT_PIPE | USB_OBJECT_BULK_PIPE; };

		status_t						QueueBulk(void *data,
											size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);
		status_t						QueueBulkV(iovec *vector,
											size_t vectorCount,
											usb_callback_func callback,
											void *callbackCookie);
};


class IsochronousPipe : public Pipe {
public:
										IsochronousPipe(Object *parent);

virtual	uint32							Type() { return USB_OBJECT_PIPE | USB_OBJECT_ISO_PIPE; };

		status_t						QueueIsochronous(void *data,
											size_t dataLength,
											usb_iso_packet_descriptor *packetDesc,
											uint32 packetCount,
											uint32 *startingFrameNumber,
											uint32 flags,
											usb_callback_func callback,
											void *callbackCookie);

		status_t						SetPipePolicy(uint8 maxQueuedPackets,
											uint16 maxBufferDurationMS,
											uint16 sampleSize);
		status_t						GetPipePolicy(uint8 *maxQueuedPackets,
											uint16 *maxBufferDurationMS,
											uint16 *sampleSize);

private:
		uint8							fMaxQueuedPackets;
		uint16							fMaxBufferDuration;
		uint16							fSampleSize;
};


class Interface : public Object {
public:
										Interface(Object *parent,
											uint8 interfaceIndex);

virtual	uint32							Type() { return USB_OBJECT_INTERFACE; };

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

private:
		uint8							fInterfaceIndex;
};


class Device : public Object {
public:
										Device(Object *parent, int8 hubPort,
											usb_device_descriptor &desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub);
virtual									~Device();

		status_t						InitCheck();

virtual	status_t						Changed(change_item **changeList,
											bool added);

virtual	uint32							Type() { return USB_OBJECT_DEVICE; };

		ControlPipe						*DefaultPipe() { return fDefaultPipe; };

virtual	status_t						GetDescriptor(uint8 descriptorType,
											uint8 index, uint16 languageID,
											void *data, size_t dataLength,
											size_t *actualLength);

		int8							DeviceAddress() const { return fDeviceAddress; };
		const usb_device_descriptor		*DeviceDescriptor() const;
		usb_speed						Speed() const { return fSpeed; };

		const usb_configuration_info	*Configuration() const;
		const usb_configuration_info	*ConfigurationAt(uint8 index) const;
		status_t						SetConfiguration(const usb_configuration_info *configuration);
		status_t						SetConfigurationAt(uint8 index);
		status_t						Unconfigure(bool atDeviceLevel);

		status_t						SetAltInterface(const usb_interface_info *interface);

		void							InitEndpoints(int32 interfaceIndex);
		void							ClearEndpoints(int32 interfaceIndex);

virtual	status_t						ReportDevice(
											usb_support_descriptor *supportDescriptors,
											uint32 supportDescriptorCount,
											const usb_notify_hooks *hooks,
											usb_driver_cookie **cookies,
											bool added, bool recursive);
virtual	status_t						BuildDeviceName(char *string,
											uint32 *index, size_t bufferSize,
											Device *device);

		int8							HubPort() const { return fHubPort; };

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

protected:
		usb_device_descriptor			fDeviceDescriptor;
		bool							fInitOK;

private:
		bool							fAvailable;
		bool							fIsRootHub;
		usb_configuration_info			*fConfigurations;
		usb_configuration_info			*fCurrentConfiguration;
		usb_speed						fSpeed;
		int8							fDeviceAddress;
		int8							fHubPort;
		ControlPipe						*fDefaultPipe;
};


class Hub : public Device {
public:
										Hub(Object *parent, int8 hubPort,
											usb_device_descriptor &desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub);
virtual									~Hub();

virtual	status_t						Changed(change_item **changeList,
											bool added);

virtual	uint32							Type() { return USB_OBJECT_DEVICE | USB_OBJECT_HUB; };

virtual	status_t						GetDescriptor(uint8 descriptorType,
											uint8 index, uint16 languageID,
											void *data, size_t dataLength,
											size_t *actualLength);

		Device							*ChildAt(uint8 index)
											{ return fChildren[index]; };

		status_t						UpdatePortStatus(uint8 index);
		status_t						ResetPort(uint8 index);
		status_t						DisablePort(uint8 index);

		void							Explore(change_item **changeList);
static	void							InterruptCallback(void *cookie,
											status_t status, void *data,
											size_t actualLength);

virtual	status_t						ReportDevice(
											usb_support_descriptor *supportDescriptors,
											uint32 supportDescriptorCount,
											const usb_notify_hooks *hooks,
											usb_driver_cookie **cookies,
											bool added, bool recursive);
virtual	status_t						BuildDeviceName(char *string,
											uint32 *index, size_t bufferSize,
											Device *device);

private:
		InterruptPipe					*fInterruptPipe;
		usb_hub_descriptor				fHubDescriptor;

		usb_port_status					fInterruptStatus[USB_MAX_PORT_COUNT];
		usb_port_status					fPortStatus[USB_MAX_PORT_COUNT];
		Device							*fChildren[USB_MAX_PORT_COUNT];
};


/*
 * A Transfer is allocated on the heap and passed to the Host Controller in
 * SubmitTransfer(). It is generated for all queued transfers. If queuing
 * succeds (SubmitTransfer() returns with >= B_OK) the Host Controller takes
 * ownership of the Transfer and will delete it as soon as it has called the
 * set callback function. If SubmitTransfer() failes, the calling function is
 * responsible for deleting the Transfer.
 * Also, the transfer takes ownership of the usb_request_data passed to it in
 * SetRequestData(), but does not take ownership of the data buffer set by
 * SetData().
 */
class Transfer {
public:
									Transfer(Pipe *pipe);
									~Transfer();

		Pipe						*TransferPipe() { return fPipe; };

		void						SetRequestData(usb_request_data *data);
		usb_request_data			*RequestData() { return fRequestData; };

		void						SetIsochronousData(usb_isochronous_data *data);
		usb_isochronous_data		*IsochronousData() { return fIsochronousData; };

		void						SetData(uint8 *buffer, size_t length);
		uint8						*Data() { return (uint8 *)fData.iov_base; };
		size_t						DataLength() { return fData.iov_len; };

		void						SetVector(iovec *vector, size_t vectorCount);
		iovec						*Vector() { return fVector; };
		size_t						VectorCount() { return fVectorCount; };
		size_t						VectorLength();

		uint16						Bandwidth() { return fBandwidth; };

		bool						IsFragmented() { return fFragmented; };
		void						AdvanceByFragment(size_t actualLength);

		status_t					InitKernelAccess();
		status_t					PrepareKernelAccess();

		void						SetCallback(usb_callback_func callback,
										void *cookie);

		void						Finished(uint32 status, size_t actualLength);

private:
		status_t					_CalculateBandwidth();

		// Data that is related to the transfer
		Pipe						*fPipe;
		iovec						fData;
		iovec						*fVector;
		size_t						fVectorCount;
		void						*fBaseAddress;
		bool						fFragmented;
		size_t						fActualLength;
		area_id						fUserArea;
		area_id						fClonedArea;

		usb_callback_func			fCallback;
		void						*fCallbackCookie;

		// For control transfers
		usb_request_data			*fRequestData;

		// For isochronous transfers
		usb_isochronous_data		*fIsochronousData;

		// For bandwidth management.
		// It contains the bandwidth necessary in microseconds
		// for either isochronous, interrupt or control transfers.
		// Not used for bulk transactions.
		uint16						fBandwidth;
};

#endif
