/*
 * Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 */
/*-
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
*/
#include <KernelExport.h>
#include <string.h>
#include <lock.h>

#include <NetBufferUtilities.h>

#include <bluetooth/HCI/btHCI_transport.h>

#include <btModules.h>
#include <l2cap.h>

#include "l2cap_internal.h"
#include "l2cap_signal.h"
#include "l2cap_upper.h"

#define BT_DEBUG_THIS_MODULE
#define SUBMODULE_NAME "lower"
#define SUBMODULE_COLOR 36
#include <btDebug.h>

status_t
l2cap_receive(HciConnection* conn, net_buffer* buffer)
{
	status_t    error = B_OK;
	uint16		dcid;
	uint16		length;

	/* Check packet */
	if (buffer->size < sizeof(l2cap_hdr_t)) {
		debugf("invalid L2CAP packet. Packet too small, len=%ld\n", buffer->size);
		gBufferModule->free(buffer);
		return EMSGSIZE;

	}

	/* Get L2CAP header */
	NetBufferHeaderReader<l2cap_hdr_t> bufferHeader(buffer);
	status_t status = bufferHeader.Status();
	if (status < B_OK) {
		return ENOBUFS;
	}

	length = bufferHeader->length = le16toh(bufferHeader->length);
	dcid = bufferHeader->dcid = le16toh(bufferHeader->dcid);

	bufferHeader.Remove(); /* pulling */

	/* Check payload size */
	if (length != buffer->size ) {
		debugf("invalid L2CAP packet. Payload length mismatch, packetlen=%d, nebufferlen=%ld\n",
			   length, buffer->size);
		gBufferModule->free(buffer);
		return EMSGSIZE;
	}

	/* Process packet */
	switch (dcid) {
		case L2CAP_SIGNAL_CID: /* L2CAP command */
			error = l2cap_process_signal_cmd(conn, buffer);
		break;

		case L2CAP_CLT_CID: /* Connectionless packet
			error = l2cap_cl_receive(buffer);*/
			flowf("CL FRAME!!\n");
		break;

		default: /* Data packet */
			error = l2cap_co_receive(conn, buffer, dcid);
		break;
	}

	return (error);

}


struct net_device_module_info* btDevices = NULL;

#if 0
#pragma mark - thread conn sched -
#endif

static thread_id sConnectionThread;


void
purge_connection(HciConnection* conn)
{
	L2capFrame* frame;

	debugf("handle=%d\n", conn->handle);

	mutex_lock(&conn->fLock);

	frame = conn->OutGoingFrames.RemoveHead();

	mutex_unlock(&conn->fLock);

//	while ( frame != NULL) {

		// Here is the place to decide how many l2cap signals we want to have
		// per l2cap packet. 1 ATM
		if (frame->type == L2CAP_C_FRAME) {
			btCoreData->TimeoutSignal(frame, bluetooth_l2cap_rtx_timeout);
			btCoreData->QueueSignal(frame);
		}

		// Add the l2cap header
		if (frame->buffer == NULL)
			panic("Malformed frame in ongoing queue");

		NetBufferPrepend<l2cap_hdr_t> bufferHeader(frame->buffer);
		status_t status = bufferHeader.Status();
		if (status < B_OK) {
			debugf("l2cap header could not be prepended!! frame code=%d\n", frame->code);
			return;

		}

		// fill
		bufferHeader->length = htole16(frame->buffer->size - sizeof(l2cap_hdr_t));
		switch (frame->type) {
			case L2CAP_C_FRAME:
				bufferHeader->dcid = L2CAP_SIGNAL_CID;
			break;
			case L2CAP_G_FRAME:
				bufferHeader->dcid = L2CAP_CLT_CID;
			break;
			default:
				bufferHeader->dcid = frame->channel->dcid;
			break;

		}

		bufferHeader.Sync();

		if (btDevices == NULL)
		if (get_module(NET_BLUETOOTH_DEVICE_NAME, (module_info**)&btDevices) != B_OK) {
			panic("l2cap: cannot get dev module");
		} // TODO: someone put it


		debugf("dev %p frame %p tolower\n", conn->ndevice, frame->buffer);

		frame->buffer->type = conn->handle;
		btDevices->send_data(conn->ndevice, frame->buffer);

//		frame = conn->OutGoingFrames.RemoveHead();
//	}

}


static status_t
connection_thread(void *)
{
	int32 code;
	ssize_t	ssizePort;
	ssize_t	ssizeRead;

	HciConnection* conn = NULL;

	// TODO: Keep this a static var
	port_id fPort = find_port(BLUETOOTH_CONNECTION_SCHED_PORT);
	if (fPort == B_NAME_NOT_FOUND)
	{
		panic("BT Connection port has been deleted");
	}

	while ((ssizePort = port_buffer_size(fPort)) != B_BAD_PORT_ID) {

		 if (ssizePort <= 0) {
		 	debugf("Error %s\n", strerror(ssizePort));
		 	snooze(500*1000);
		    continue;
		 }

		if (ssizePort > (ssize_t) sizeof(conn)) {
		 	debugf("Message too big %ld\n", ssizePort);
		 	snooze(500*1000);
			continue;
		}

		ssizeRead = read_port(fPort, &code, &conn, ssizePort);

		if (ssizeRead != ssizePort) {
		 	debugf("Missmatch size port=%ld read=%ld\n", ssizePort, ssizeRead);
		 	snooze(500*1000);
		    continue;
		}

		purge_connection(conn);
	}

	return B_OK;
}


status_t
InitializeConnectionPurgeThread()
{

	port_id fPort = find_port(BLUETOOTH_CONNECTION_SCHED_PORT);
	if (fPort == B_NAME_NOT_FOUND)
	{
		fPort = create_port(16, BLUETOOTH_CONNECTION_SCHED_PORT);
		debugf("Connection purge port created %ld\n",fPort);
	}

	// This thread has to catch up connections before first package is sent.
	sConnectionThread = spawn_kernel_thread(connection_thread,
				"bluetooth connection purge", B_URGENT_DISPLAY_PRIORITY, NULL);

	if (sConnectionThread >= B_OK)
		return resume_thread(sConnectionThread);
	else
		return B_ERROR;
}


status_t
QuitConnectionPurgeThread()
{
	status_t status;

	port_id fPort = find_port(BLUETOOTH_CONNECTION_SCHED_PORT);
	if (fPort != B_NAME_NOT_FOUND)
		close_port(fPort);

	flowf("Connection port deleted\n");
	wait_for_thread(sConnectionThread, &status);
	return status;
}


void
SchedConnectionPurgeThread(HciConnection* conn)
{
	port_id port = find_port(BLUETOOTH_CONNECTION_SCHED_PORT);

	HciConnection* temp = conn;

	if (port == B_NAME_NOT_FOUND)
		panic("BT Connection Port Deleted");

	status_t error = write_port(port, (uint32) conn, &temp, sizeof(conn));

	//debugf("error post %s port=%ld size=%ld\n", strerror(error), port, sizeof(conn));

	if (error != B_OK)
		panic("BT Connection sched failed");

}
