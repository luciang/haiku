#ifndef USB_HID_H
#define USB_HID_H

// (Partial) USB Class Definitions for HID Devices, version 1.11
// Reference: http://www.usb.org/developers/devclass_docs/hid1_11.pdf

#define USB_HID_DEVICE_CLASS 			0x03
#define USB_HID_CLASS_VERSION			0x0100

enum { // HID Interface Subclasses
	USB_HID_INTERFACE_NO_SUBCLASS = 0x00,	//  No Subclass
	USB_HID_INTERFACE_BOOT_SUBCLASS			//	Boot Interface Subclass
};

enum { // HID Class-Specific descriptor subtypes
	USB_HID_DESCRIPTOR_HID						= 0x21,
	USB_HID_DESCRIPTOR_REPORT,
	USB_HID_DESCRIPTOR_PHYSICAL
};

enum { // HID Class-specific requests
	USB_REQUEST_HID_GET_REPORT		= 0x01,
	USB_REQUEST_HID_GET_IDLE,
	USB_REQUEST_HID_GET_PROTOCOL,
	
	USB_REQUEST_HID_SET_REPORT = 0x09,
	USB_REQUEST_HID_SET_IDLE,
	USB_REQUEST_HID_SET_PROTOCOL
};

typedef struct
{
	uint8	length;
	uint8	descriptor_type;
	uint16	hid_version;
	uint8	country_code;
	uint8	num_descriptors;
	struct
	{
		uint8	descriptor_type;
		uint16	descriptor_length;
	} _PACKED descriptor_info [1];
} _PACKED usb_hid_descriptor;


/*
	Usage Pages/IDs
*/

enum
{
	USAGE_PAGE_GENERIC_DESKTOP = 0x1,
	USAGE_PAGE_SIMULATION,
	USAGE_PAGE_VR,
	USAGE_PAGE_SPORT,
	USAGE_PAGE_GAME,
	USAGE_PAGE_GENERIC,
	USAGE_PAGE_KEYBOARD,
	USAGE_PAGE_LED,
	USAGE_PAGE_BUTTON,
	USAGE_PAGE_ORDINAL,
	USAGE_PAGE_TELEPHONY,
	USAGE_PAGE_CONSUMER,
	USAGE_PAGE_DIGITIZER,
	USAGE_PAGE_PID = 0xf,
	USAGE_PAGE_UNICODE,
	USAGE_PAGE_ALPHANUM_DISPLAY = 0x14,
	USAGE_PAGE_MEDICAL = 0x40,
	USGAE_PAGE_MICROSOFT = 0xff00
};

/* Page 1: Generic Desktop */

enum
{
	USAGE_ID_POINTER = 0x1,
	USAGE_ID_MOUSE,
	USAGE_ID_JOYSTICK = 0x4,
	USAGE_ID_GAMEPAD,
	USAGE_ID_KEYBOARD,
	USAGE_ID_KEYPAD,
	USAGE_ID_MULTIAXIS = 0x8,
	
	USAGE_ID_X = 0x30,
	USAGE_ID_Y,
	USAGE_ID_Z,
	USAGE_ID_RX,
	USAGE_ID_RY,
	USAGE_ID_RZ,
	USAGE_ID_SLIDER,
	USAGE_ID_DIAL,
	USAGE_ID_WHEEL,
	USAGE_ID_HAT_SWITCH,
	USAGE_ID_COUNTED_BUFFER,
	USAGE_ID_BYTE_COUNT,
	USAGE_ID_MOTION_WAKEUP,
	USAGE_ID_START,
	USAGE_ID_SELECT,
	USAGE_ID_VX = 0x40,
	USAGE_ID_VY,
	USAGE_ID_VZ,
	USAGE_ID_VBRX,
	USAGE_ID_VBRY,
	USAGE_ID_VBRZ,
	USAGE_ID_VNO,
	USAGE_ID_FEATURE_NOTIFICATION
};

/* Page 2: Simulation */

enum
{
	USAGE_ID_RUDDER = 0xBA,
	USAGE_ID_THROTTLE = 0xBB,
};

#endif	// USB_HID_H
