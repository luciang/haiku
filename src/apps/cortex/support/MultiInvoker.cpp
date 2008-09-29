////////////////////////////////////////////////////////////
// MultiInvoker.cpp
// ----------------
// Implements the MultiInvoker class.
//
// Copyright 1999, Be Incorporated.   All Rights Reserved.
// This file may be used under the terms of the Be Sample
// Code License.

#include <Messenger.h>
#include "MultiInvoker.h"

MultiInvoker::MultiInvoker()
{
	m_message = 0;
	m_timeout = B_INFINITE_TIMEOUT;
	m_replyHandler = 0;
}

MultiInvoker::MultiInvoker(const MultiInvoker& src)
{
	Clone(src);
}

MultiInvoker::~MultiInvoker()
{
	Clear();
}

MultiInvoker& MultiInvoker::operator=(const MultiInvoker& src)
{
	if (this != &src) {
		Clear();
		Clone(src);
	}
	return *this;
}

void MultiInvoker::Clear()
{
	delete m_message;
	int32 i=CountTargets();
	while (--i >=0) {
		RemoveTarget(i);
	}
}

void MultiInvoker::Clone(const MultiInvoker& src)
{
	m_message = new BMessage(*src.Message());
	int32 len=src.CountTargets();
	for (int32 i=0; i<len; i++) {
		AddTarget(src.TargetAt(i));
	}
	m_timeout = src.Timeout();
	m_replyHandler = src.HandlerForReply();
}

void MultiInvoker::SetMessage(BMessage* message)
{
	delete m_message;
	m_message = message;
}

BMessage* MultiInvoker::Message() const
{
	return m_message;
}

uint32 MultiInvoker::Command() const
{
	return (m_message) ? m_message->what : 0;
}

status_t MultiInvoker::AddTarget(const BHandler* h, const BLooper* loop)
{
	status_t err;
	BMessenger* msgr = new BMessenger(h, loop, &err);
	if (err == B_OK)
		m_messengers.AddItem(msgr);
	else
		delete msgr;
	return err;	
}

status_t MultiInvoker::AddTarget(BMessenger* msgr)
{
	if (msgr) {
		m_messengers.AddItem(msgr);
		return B_OK;
	} else {
		return B_BAD_VALUE;
	}
}

void MultiInvoker::RemoveTarget(const BHandler* h)
{
	int32 i = IndexOfTarget(h);
	if (i >= 0)
		RemoveTarget(i);
}

void MultiInvoker::RemoveTarget(int32 index)
{
	BMessenger* msgr = static_cast<BMessenger*>
		(m_messengers.RemoveItem(index));
	delete msgr;
}

int32 MultiInvoker::IndexOfTarget(const BHandler* h) const
{
	int32 len = CountTargets();
	for (int32 i=0; i<len; i++) {
		BMessenger* msgr = MessengerAt(i);
		if (msgr && msgr->Target(0) == h) {
			return i;
		}
	}
	return -1;
}

int32 MultiInvoker::CountTargets() const
{
	return m_messengers.CountItems();
}

BHandler* MultiInvoker::TargetAt(int32 index, BLooper** looper) const
{
	BMessenger* msgr = MessengerAt(index);
	if (msgr) {
		return msgr->Target(looper);
	} else {
		if (looper) *looper = 0;
		return 0;
	}
}

BMessenger* MultiInvoker::MessengerAt(int32 index) const
{
	return static_cast<BMessenger*>
		(m_messengers.ItemAt(index));
}

bool MultiInvoker::IsTargetLocal(int32 index) const
{
	BMessenger* msgr = MessengerAt(index);
	return (msgr) ? msgr->IsTargetLocal() : false;
}

void MultiInvoker::SetTimeout(bigtime_t timeout)
{
	m_timeout = timeout;
}

bigtime_t MultiInvoker::Timeout() const
{
	return m_timeout;
}

void MultiInvoker::SetHandlerForReply(BHandler* h)
{
	m_replyHandler = h;
}

BHandler* MultiInvoker::HandlerForReply() const
{
	return m_replyHandler;
}

status_t MultiInvoker::Invoke(BMessage* msg)
{
	BMessage* sendMsg = (msg) ? msg : m_message;
	if (! sendMsg)
		return B_BAD_VALUE;

	status_t err, finalResult=B_OK;	
	BMessage replyMsg;	
	int32 len = CountTargets();
	for (int32 i=0; i<len; i++) {
		BMessenger* msgr = MessengerAt(i);
		if (msgr) {
			err = msgr->SendMessage(sendMsg,
				HandlerForReply(), m_timeout);
			if (err != B_OK) finalResult = err;
		}
	}
	return finalResult;
}
