/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 */

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <ByteOrder.h>
#include <Drivers.h>
#include <USB_spec.h>
#include <USB.h>

#include <net/net_buffer.h>
#include "snet_buffer.h"

#include <bluetooth_util.h>
#include <btHCI.h>

#define BT_DEBUG_THIS_MODULE
#include <btDebug.h>

#include "h2generic.h"
#include "h2transactions.h"
#include "h2util.h"

#include "h2cfg.h"

int32 api_version = B_CUR_DRIVER_API_VERSION;

/* Modules */
static char* usb_name = B_USB_MODULE_NAME;
static char* hci_name = B_BT_HCI_MODULE_NAME;

usb_module_info *usb = NULL;
bt_hci_module_info *hci = NULL;
struct net_buffer_module_info *nb = NULL;

/* Driver Global data */
static char	 *publish_names[MAX_BT_GENERIC_USB_DEVICES];

int32 		dev_count = 0;	/* number of connected devices */
static bt_usb_dev*	bt_usb_devices[MAX_BT_GENERIC_USB_DEVICES];
sem_id 		dev_table_sem = -1; /* sem to synchronize access to device table */

usb_support_descriptor supported_devices[] =
{
    /* Generic Bluetooth USB device */
    /* Class, SubClass, and Protocol codes that describe a Bluetooth device */
	{ UDCLASS_WIRELESS, UDSUBCLASS_RF, UDPROTO_BLUETOOTH , 0 , 0 },

    /* Generic devices	*/
	/* Broadcom BCM2035 */
	{ 0, 0, 0, 0x0a5c, 0x200a },
	{ 0, 0, 0, 0x0a5c, 0x2009 },
	
	/* Devices taken from the linux Driver */
	/* AVM BlueFRITZ! USB v2.0 */
	{ 0, 0, 0, 0x057c   , 0x3800 },
	/* Bluetooth Ultraport Module from IBM */
	{ 0, 0, 0, 0x04bf   , 0x030a },
	/* ALPS Modules with non-standard id */
	{ 0, 0, 0, 0x044e   , 0x3001 },
	{ 0, 0, 0, 0x044e   , 0x3002 },
	/* Ericsson with non-standard id */
	{ 0, 0, 0, 0x0bdb   , 0x1002 }
};	

/* add a device to the list of connected devices */
static bt_usb_dev*
spawn_device(const usb_device* usb_dev)
{
	int32 i;
	status_t err = B_OK;	
	bt_usb_dev* new_bt_dev = NULL;	

	flowf("add_device()\n");

	/* 16 usb dongles... u are unsane */
	if (dev_count >= MAX_BT_GENERIC_USB_DEVICES) {
		flowf("device table full\n");
		goto exit;
	}
	
	/* try the allocation */
	new_bt_dev = (bt_usb_dev*)malloc(sizeof(bt_usb_dev));
	if ( new_bt_dev == NULL ) {
		flowf("no memoery allocating\n");
		goto exit;
	}
	memset(new_bt_dev, 0, sizeof(bt_usb_dev) );

	/* We will need this sem for some flow control */
	new_bt_dev->cmd_complete = create_sem(1, ID "cmd_complete");
	if (new_bt_dev->cmd_complete < 0) {
		err = new_bt_dev->cmd_complete;
		goto bail0;
	}

	/* and this for something else */
	new_bt_dev->lock = create_sem(1, ID "lock");
	if (new_bt_dev->lock < 0) {
		err = new_bt_dev->lock;
		goto bail1;
	}

	/* find a free slot and fill out the name */		
	acquire_sem(dev_table_sem);	
	for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
		if (bt_usb_devices[i] == NULL) {
			bt_usb_devices[i] = new_bt_dev;
			sprintf(new_bt_dev->name, "%s/%ld", DEVICE_PATH, i);
			new_bt_dev->num = i;
			debugf("added device %p %ld %s\n", bt_usb_devices[i] ,new_bt_dev->num,new_bt_dev->name);	
			break;
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);	

	/* In the case we cannot us */
	if (bt_usb_devices[i] != new_bt_dev) {
	    flowf("Device could not be added\n");
		goto bail2;
	}

	new_bt_dev->dev = usb_dev;
	/* TODO am i actually gonna use this? */
	new_bt_dev->open_count = 0;

	dev_count++;
	return new_bt_dev;

bail2:
	delete_sem(new_bt_dev->lock);
bail1:
	delete_sem(new_bt_dev->cmd_complete);
bail0:
	free(new_bt_dev);
exit:	
	return new_bt_dev;
}


/* remove a device from the list of connected devices */
static void
kill_device(bt_usb_dev* dev)
{
	uint16 i;
	debugf("remove_device(%p)\n", dev);
		
	delete_sem(dev->lock);
	delete_sem(dev->cmd_complete);
	
	free(dev);
	dev_count--;
}


bt_usb_dev*
fetch_device(bt_usb_dev* dev, hci_id hid)
{
    int i;
    
	debugf("(%p)\n", dev);
	
	acquire_sem(dev_table_sem);
    if (dev != NULL)    
	    for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
	        /* somehow the device is still around */
		    if (bt_usb_devices[i] == dev) {
		        release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
		        return bt_usb_devices[i];			
		    }
		}	
	else
	    for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
	        /* somehow the device is still around */
		    if (bt_usb_devices[i] != NULL && bt_usb_devices[i]->hdev == hid) {
		        release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
		        return bt_usb_devices[i];			
		    }
		}	
	

	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
	
    return NULL;    
}

#if 0
#pragma mark -
#endif

static bt_hci_transport bt_usb_hooks = 
{
	NULL,
	NULL,
	NULL,
	NULL,
	H2,
	"H2 Bluetooth Device"
};

/* called by USB Manager when device is added to the USB */
static status_t
device_added(const usb_device* dev, void** cookie)
{
	const usb_interface_info* 		interface;
	const usb_device_descriptor* 	desc;	
	const usb_configuration_info*	config;
	const usb_interface_info*		uif;
	const usb_endpoint_info*		ep;
    
    status_t 	err = B_ERROR;
	bt_usb_dev* new_bt_dev = spawn_device(dev);
    int e, i;	

	debugf("device_added(%ld, %p)\n", dev, new_bt_dev);

	if (new_bt_dev == NULL) {
		flowf("Couldn't allocate device record.\n");
		err = ENOMEM;
		goto bail_no_mem;	
	}

	/* we only have 1 configuration number 0 */
	config = usb->get_nth_configuration(dev, 0);
	//dump_usb_configuration_info(config); 
	if (config == NULL) {
		flowf("couldn't get default config.\n");
		err = B_ERROR;
		goto bail;
	}

	debugf("found %ld alt interfaces.\n", config->interface->alt_count);
	
	/* set first interface */
	interface = &config->interface->alt[0];
	err = usb->set_alt_interface(new_bt_dev->dev, interface);
	
	if (err != B_OK) {
		debugf("set_alt_interface() returned %ld.\n", err);
		goto bail;
	}

	/* call set_configuration() only after calling set_alt_interface()*/
	err = usb->set_configuration(dev, config);
	if (err != B_OK) {
		debugf("set_configuration() returned %ld.\n", err);
		goto bail;
	}
			
	/* Place to find out whats our concrete device and set up  some special info to our driver */
	/* TODO: if this code increases too much reconsider this implementation*/
	desc = usb->get_device_descriptor(dev);
	if ( desc->vendor_id == 0x0a5c && desc->product_id == 0x200a && desc->product_id == 0x2009) {
		// Tecom Device VENDOR_ID 0x0a5c PRODUCT_ID 0x2035
		new_bt_dev->driver_info = BT_WILL_NEED_A_RESET | BT_SCO_NOT_WORKING;
	} 
	/*
	else if ( desc->vendor_id == YOUR_VENDOR_HERE && desc->product_id == YOUR_PRODUCT_HERE ) {
		 YOUR_SPECIAL_FLAGS_HERE
	}
	*/

	if (new_bt_dev->driver_info & BT_IGNORE_THIS_DEVICE){
		err = ENODEV;
		goto bail;
	}
			
	// security check
	if (config->interface->active->descr->interface_number > 0){
		debugf("Strange condition happened %d\n", config->interface->active->descr->interface_number);
		err = B_ERROR;
		goto bail;
	}
	
	debugf("Found %ld interfaces. Expected 3\n", config->interface_count);	
	/* Find endpoints that we need */
	uif = config->interface->active;
	for (e = 0; e < uif->descr->num_endpoints; e++) {

		ep = &uif->endpoint[e];
		switch (ep->descr->attributes & USB_ENDPOINT_ATTR_MASK) 
		{
			case USB_ENDPOINT_ATTR_INTERRUPT:
				if (ep->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) 
				{
					new_bt_dev->intr_in_ep = ep;
					new_bt_dev->max_packet_size_intr_in = ep->descr->max_packet_size;
					flowf("INT in\n");							
				} else 
				{
					;
					flowf("INT out\n");
				}
			break;

			case USB_ENDPOINT_ATTR_BULK:
				if (ep->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
				{
					new_bt_dev->bulk_in_ep  = ep;
					new_bt_dev->max_packet_size_bulk_in = ep->descr->max_packet_size;;
					flowf("BULK int\n");
				} else
				{
					new_bt_dev->bulk_out_ep = ep;
					new_bt_dev->max_packet_size_bulk_out = ep->descr->max_packet_size;;
					flowf("BULK out\n");
				}
			break;			
		}
	}
	
	if (!new_bt_dev->bulk_in_ep || !new_bt_dev->bulk_out_ep || !new_bt_dev->intr_in_ep) {
		flowf("Minimal # endpoints for BT not found\n");
		goto bail;
	}

	// Look into the devices suported to understand this
    if (new_bt_dev->driver_info & BT_DIGIANSWER)
		new_bt_dev->ctrl_req = USB_TYPE_VENDOR;
	else
		new_bt_dev->ctrl_req = USB_TYPE_CLASS;

    new_bt_dev->connected = true;

	/* set the cookie that will be passed to other USB
	   hook functions (currently device_removed() is the only other) */
	*cookie = new_bt_dev;		    
	debugf("Ok %p\n",bt_usb_devices[0]);	
	return B_OK;

bail:		
	kill_device(new_bt_dev);
bail_no_mem:	
	*cookie = NULL;
done:
	return err;
}


/* called by USB Manager when device is removed from the USB */
static status_t
device_removed(void* cookie)
{
	int32 i;
	void* item;
	bt_usb_dev* bdev = (bt_usb_dev*) fetch_device(cookie, 0);
	
	debugf("device_removed(%p)\n", bdev);

    if (bdev == NULL) {    
        flowf("Weird condition...\n");
        return B_ERROR;
    }
	// TODO: Consider some other place
	// TX
	for (i = 0; i < BT_DRIVER_TXCOVERAGE; i++) {
		if (i = BT_COMMAND)
			while ((item = list_remove_head_item(&bdev->nbuffersTx[i])) != NULL) {
				snb_free(item);			
			}
		else
			while ((item = list_remove_head_item(&bdev->nbuffersTx[i])) != NULL) {
				nb_destroy(item);			
			}
	
	}
	// RX
	for (i = 0; i < BT_DRIVER_RXCOVERAGE; i++) {
	    nb_destroy(bdev->nbufferRx[i]);				
	}
	snb_free(bdev->eventRx);
	
    purge_room(&bdev->eventRoom);
    purge_room(&bdev->aclRoom);
	// TODO: Consider some other place
    
    if (hci != NULL)
        hci->UnregisterDriver(bdev->hdev);    

	bdev->connected = false;
	
	/* TODO: maybe we still need this struct for close and free hooks */
	kill_device(bdev);

	return B_OK;
}


static usb_notify_hooks notify_hooks = 
{
	&device_added,
	&device_removed
};

#if 0
#pragma mark -
#endif

/* implements the POSIX open() */
static status_t
device_open(const char *name, uint32 flags, void **cookie)
{
	status_t err = ENODEV;
	bt_usb_dev* bdev = NULL;
	hci_id 		hdev;
	int i;
		
	flowf("device_open()\n");

	acquire_sem(dev_table_sem);
	for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
		if (bt_usb_devices[i] && !strcmp(name, bt_usb_devices[i]->name)) {
			bdev = bt_usb_devices[i];
			break;
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
	
	if (bdev == NULL) {
		flowf("Device not found in the open list!");
		*cookie = NULL;
		return B_ERROR;
	}	
	
	acquire_sem(bdev->lock);	
	// Set HCI_RUNNING
	if ( TEST_AND_SET(&bdev->state, RUNNING) ) {
	    flowf("dev already running! - reOpened device!\n");
	    return B_ERROR;
	}	

	// TX structures 			
	for (i = 0; i < BT_DRIVER_TXCOVERAGE; i++) {
		list_init(&bdev->nbuffersTx[i]);		
		bdev->nbuffersPendingTx[i] = 0;		
	}
	
	// RX structures
	bdev->eventRx = NULL;
	for (i = 0; i < BT_DRIVER_RXCOVERAGE; i++) {
		bdev->nbufferRx[i] = NULL;
	}
	
	
	// dumping the USB frames	
    init_room(&bdev->eventRoom);
    init_room(&bdev->aclRoom);
    //Init_room(new_bt_dev->scoRoom);
    
   	list_init(&bdev->snetBufferRecycleTrash);		
    			
	// Allocate set and register the HCI device
	if (hci != NULL) {
		//	TODO: Fill the transport descriptor
	    hci->RegisterDriver(&bt_usb_hooks, &hdev, (void*)bdev);
	    
    	if ( err != B_OK ) 
    	{ 
    		flowf("Impossible to register a hci device.\n");
    		hdev = bdev->num; /* XXX: Lets try to go on*/
    	}
    }
    else {
        hdev = bdev->num;
    }    	
	bdev->hdev = hdev;		

	//  H: set the special flags

    //  EVENTS
	err = submit_rx_event(bdev);
	if (err != B_OK)    
	    goto unrun;	    
#if BT_DRIVER_SUPPORTS_ACL        	
	// ACL
	for (i = 0; i < MAX_ACL_IN_WINDOW; i++)  {		
		err = submit_rx_acl(bdev);
		if (err != B_OK && i == 0 )
		    goto unrun;			    
	}
#endif

#if BT_DRIVER_SUPPORTS_SCO
	// TODO:  SCO / eSCO
#endif	
		
	*cookie = bdev;	
	release_sem(bdev->lock);	

	return B_OK;

unrun:
    CLEAR_BIT(bdev->state, RUNNING);	// Set the flaq in the HCI world
	flowf("Queuing failed device stops running\n");

	return err;
}


/* called when a client calls POSIX close() on the driver, but I/O
 ** requests may still be pending */
static status_t
device_close(void *cookie)
{
	bt_usb_dev* bdev = (bt_usb_dev*)cookie;

	if (bdev == NULL)
		panic("bad cookie");
		
	debugf("device_close() called on %s\n", DEVICE_PATH, bdev->hdev );

	
	if (!TEST_AND_CLEAR(&bdev->state, RUNNING) ) {
		flowf("Device wasnt running!!!\n");	
	}
	
	flowf("Stopping device and cancelling queues...\n");
	
	if ( bdev->intr_in_ep != NULL ) {
		usb->cancel_queued_transfers(bdev->intr_in_ep->handle);
		
	} else {
		flowf("Cancelling impossible EVENTS\n");
	}
	
	if (bdev->bulk_in_ep!=NULL) {
		usb->cancel_queued_transfers(bdev->bulk_in_ep->handle);
	} else {
		flowf("Cancelling impossible ACL in\n");
	}
	
	if (bdev->bulk_out_ep!=NULL) {
		usb->cancel_queued_transfers(bdev->bulk_out_ep->handle);
	} else {
		flowf("Cancelling impossible ACL out\n");
	}
    
    // TODO: Kill if its not connected?

	return B_OK;
}


/* called after device_close(), when all pending I/O requests have
 * returned */
static status_t
device_free (void *cookie)
{
	status_t err = B_OK;
	bt_usb_dev* dev = (bt_usb_dev*)cookie;
	
	debugf("device_free() called on \"%s %ld\"\n",DEVICE_PATH, dev->num);
		

	if (--dev->open_count == 0) {

		/* GotoLowPower */
		// interesting .....
	}
	else {
		/* The last client has closed, and the device is no longer
		   connected, so remove it from the list. */
		   
	}
	
	// TODO: Kill if its not connected?

	return err;
}


/* implements the POSIX ioctl() */
static status_t
device_control(void *cookie, uint32 msg, void *params, size_t size)
{
	status_t 	err = B_ERROR;
	bt_usb_dev*	dev = (bt_usb_dev*)cookie;
	snet_buffer* snbuf;
	TOUCH(size);

	debugf("ioctl() opcode %ld size %ld.\n", msg, size);
	
	if (dev == NULL) {
		flowf("Bad cookie\n");
		return B_BAD_VALUE;
	}

	if (params == NULL) {
		flowf("Invalid pointer control\n");
		return B_BAD_VALUE;
	}
	
	acquire_sem(dev->lock);
		
	switch (msg) {
		case ISSUE_BT_COMMAND:
#ifdef BT_IOCTLS_PASS_SIZE			
		 	if (size == 0) {
				flowf("Invalid size control\n");
				err = B_BAD_VALUE;				
				break;
			}
#else
			size = (*((size_t*)params));
			((size_t*)params)++;
#endif			
		   	
		   	// TODO: Reuse from some TXcompleted queue		    	   		    
		    snbuf = snb_create(size);
		    snb_put(snbuf, params, size);
			
			err = send_command(dev->hdev, snbuf);
		    		
		break;
		
		case ISSUE_STATICS:
		    memcpy(params, &dev->stat, sizeof(bt_hci_statistics));
		    err = B_OK;
		break;		
		
	default:
		debugf("Invalid opcode %ld.\n", msg);
		err = B_DEV_INVALID_IOCTL;
		break;
	}
	
	release_sem(dev->lock);
	return err;
}


/* implements the POSIX read() */
static status_t
device_read(void *cookie, off_t pos, void *buf, size_t *count)
{
	debugf("Reading... pos = %ld || count = %ld\n", pos, *count);
		
	*count = 0;
	return B_OK;
}


/* implements the POSIX write() */
static status_t
device_write(void *cookie, off_t pos, const void *buf, size_t *count)
{
	flowf("device_write()\n");
	
	return B_ERROR;
}

#if 0
#pragma mark -
#endif

/* called each time the driver is loaded by the kernel */
status_t
init_driver(void)
{
	int j;
	flowf("init_driver()\n");
	
	// HCI MODULE INITS
	if (get_module(hci_name,(module_info**)&hci) != B_OK) {
		debugf("cannot get module \"%s\"\n", hci_name);
#ifndef BT_SURVIVE_WITHOUT_HCI
		return B_ERROR;
#endif
	}
	debugf("hci module at %p\n", hci);	

	// USB MODULE INITS	
	if (get_module(usb_name,(module_info**)&usb) != B_OK) {
		debugf("cannot get module \"%s\"\n", usb_name);
		goto err_release;
	}
	debugf("usb module at %p\n", usb);


	if (get_module(NET_BUFFER_MODULE_NAME,(module_info**)&nb) != B_OK) {
		debugf("cannot get module \"%s\"\n", NET_BUFFER_MODULE_NAME);
#ifndef BT_SURVIVE_WITHOUT_NET_BUFFERS
		goto err_release;
#endif		
	}
	debugf("nb module at %p\n", nb);

	// GENERAL INITS
	dev_table_sem = create_sem(1, ID "dev_table_lock");
	if (dev_table_sem < 0) {
		goto err;
	}
	
	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {
	    bt_usb_devices[j] = NULL;
	}
	
	/* After here device_added and publish devices hooks are called 
	   be carefull USB devs */
	usb->register_driver(DEVICE_NAME, supported_devices, 1, NULL);
	usb->install_notify(DEVICE_NAME, &notify_hooks);

	return B_OK;
	
err: 	// Releasing 
	put_module(usb_name);
err_release:	
	put_module(hci_name);
	return B_ERROR;
}


/* called just before the kernel unloads the driver */
void
uninit_driver(void)
{
	int32 j;
	 
	flowf("uninit_driver()\n");
	
	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {

		if (publish_names[j] != NULL) 
			free(publish_names[j]);
			
		if (bt_usb_devices[j] != NULL) {
		    //	if (connected_dev != NULL) {
            //		debugf("Device %p still exists.\n",	connected_dev);
            //	}
			kill_device(bt_usb_devices[j]);
			bt_usb_devices[j] = NULL;
		}

	}
	
	usb->uninstall_notify(DEVICE_NAME);
	usb->register_driver(DEVICE_NAME, supported_devices, 1, NULL);
	
	/* Releasing modules */
	put_module(usb_name);
	put_module(hci_name);
	// TODO: netbuffers
	
	delete_sem(dev_table_sem);
}


const char**
publish_devices(void)
{
	int32 j;
	int32 i = 0;

	char* str;
	
	flowf("publish_devices()\n");

	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {
		if (publish_names[j]) {
			free(publish_names[j]);
			publish_names[j] = NULL;
		}
	}
	
	acquire_sem(dev_table_sem);
	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) 
	{
		if (bt_usb_devices[j] != NULL && bt_usb_devices[j]->connected)
		{
			str = strdup(bt_usb_devices[j]->name);
			if (str) {
				publish_names[i++] = str;
				debugf("publishing %s\n", bt_usb_devices[j]->name);
			}
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
	
	publish_names[i] = NULL;
    debugf("published %ld devices\n", i);

//  TODO: this method might make better memory use	
//	dev_names = (char**)malloc(sizeof (char*) * (dev_count+1));
//	if (dev_names) {
//		for (i = 0; i < MAX_NUM_DEVS; i++) {
//			if ((dev != NULL) &&
//					(dev_names[i] = (char*)malloc(strlen(DEVICE_PATH)+2/* num + \n */))) {
//				sprintf(dev_names[i], "%s%ld", DEVICE_PATH, dev->num);
//				debugf("publishing \"%s\"\n", dev_names[i]);
//			}
//		}
	
	return (const char**)publish_names;
}


static device_hooks hooks = {
	device_open, 			
	device_close, 			
	device_free,			
	device_control, 		
	device_read,			
	device_write,
	NULL,
	NULL,
	NULL,
	NULL		 
};


device_hooks*
find_device(const char* name)
{
	debugf("find_device(%s)\n", name);
	
	return &hooks;
}
