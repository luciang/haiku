//------------------------------------------------------------------------------
//	Copyright (c) 2004, Niels S. Reedijk
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.

#include "usb_p.h"

Pipe::Pipe( Device *dev , Direction &dir , uint8 &endpointaddress )
{
	m_direction = dir;
	m_device = dev;
	m_endpoint = endpointaddress;
	if ( m_device != NULL )
		m_bus = m_device->GetBusManager();
}

Pipe::~Pipe()
{
}

int8 Pipe::GetDeviceAddress()
{
	if ( m_device == NULL )
		return -1;
	else
		return m_device->GetAddress();
}

ControlPipe::ControlPipe( Device *dev , Direction dir , uint8 endpointaddress )
           : Pipe( dev , dir , endpointaddress )
{
	
}	          

ControlPipe::ControlPipe( BusManager *bus , int8 dev_address , Direction dir , Speed speed , uint8 endpointaddress )
			: Pipe( NULL , dir , endpointaddress )
{
	m_bus = bus;
	m_deviceaddress = dev_address;
	m_speed = speed;
}

int8 ControlPipe::GetDeviceAddress()
{
	if ( m_device == NULL )
		return m_deviceaddress;
	else
		return m_device->GetAddress();
}

status_t ControlPipe::SendRequest( uint8 request_type , uint8 request , uint16 value ,
	                  uint16 index , uint16 length , void *data ,
	                  size_t data_len , size_t *actual_len )
{
	usb_request_data req;
	
	req.RequestType = request_type;
	req.Request = request;
	req.Value = value;
	req.Index = index;
	req.Length = length;
	
	return SendControlMessage( &req , data , data_len , actual_len , 3 * 1000 * 1000 ); 
}

status_t ControlPipe::SendControlMessage( usb_request_data *command , void *data ,
	                             size_t data_length , size_t *actual_length , 
	                             bigtime_t timeout )
{
	// this method should build an usb packet (new class) with the needed data
	Transfer *transfer = new Transfer( this , true );
	
	transfer->SetRequestData( command );
	transfer->SetBuffer( (uint8 *)data );
	transfer->SetBufferLength( data_length );
	transfer->SetActualLength( actual_length );
	
	status_t retval = m_bus->SubmitTransfer( transfer );
	return retval;
}
