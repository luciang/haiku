#ifndef _BLUETOOTH_SERVER_PRIVATE_H
#define _BLUETOOTH_SERVER_PRIVATE_H


#define BLUETOOTH_SIGNATURE "application/x-vnd.Be-bluetooth_server"

/* Kit Comunication */

// LocalDevice 
#define BT_MSG_COUNT_LOCAL_DEVICES		'btCd'
#define BT_MSG_ACQUIRE_LOCAL_DEVICE     'btAd'
#define BT_MSG_HANDLE_SIMPLE_REQUEST    'btsR'
#define BT_MSG_ADD_DEVICE               'btDD'
#define BT_MSG_REMOVE_DEVICE            'btrD'
#define BT_MSG_GET_PROPERTY             'btgP'

// Discovery
#define BT_MSG_INQUIRY_STARTED    'IqSt'
#define BT_MSG_INQUIRY_COMPLETED  'IqCM'
#define BT_MSG_INQUIRY_TERMINATED 'IqTR'
#define BT_MSG_INQUIRY_ERROR      'IqER'
#define BT_MSG_INQUIRY_DEVICE     'IqDE'


#endif
