//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
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
//
//	File Name:		MessageRunner.cpp
//	Author:			Ingo Weinhold (bonefish@users.sf.net)
//	Description:	A BMessageRunner periodically sends a message to a
//                  specified target.
//------------------------------------------------------------------------------
#include <Application.h>
#include <AppMisc.h>
#include <MessageRunner.h>
#include <RegistrarDefs.h>
#include <Roster.h>

// constructor
/*!	\brief Creates and initializes a new BMessageRunner.

	The target for replies to the delivered message(s) is \c be_app_messenger.

	The success of the initialization can (and should) be asked for via
	InitCheck().

	\note As soon as the last message has been sent, the message runner
		  becomes unusable. InitCheck() will still return \c B_OK, but
		  SetInterval(), SetCount() and GetInfo() will fail.

	\param target Target of the message(s).
	\param message The message to be sent to the target.
	\param interval Period of time before the first message is sent and
		   between messages (if more than one shall be sent) in microseconds.
	\param count Specifies how many times the message shall be sent.
		   A value less than \c 0 for an unlimited number of repetitions.
*/
BMessageRunner::BMessageRunner(BMessenger target, const BMessage *message,
							   bigtime_t interval, int32 count)
	: fToken(-1)
{
	InitData(target, message, interval, count, be_app_messenger);
}

// constructor
/*!	\brief Creates and initializes a new BMessageRunner.

	This constructor version additionally allows to specify the target for
	replies to the delivered message(s).

	The success of the initialization can (and should) be asked for via
	InitCheck().

	\note As soon as the last message has been sent, the message runner
		  becomes unusable. InitCheck() will still return \c B_OK, but
		  SetInterval(), SetCount() and GetInfo() will fail.

	\param target Target of the message(s).
	\param message The message to be sent to the target.
	\param interval Period of time before the first message is sent and
		   between messages (if more than one shall be sent) in microseconds.
	\param count Specifies how many times the message shall be sent.
		   A value less than \c 0 for an unlimited number of repetitions.
	\param replyTo Target replies to the delivered message(s) shall be sent to.
*/
BMessageRunner::BMessageRunner(BMessenger target, const BMessage *message,
							   bigtime_t interval, int32 count,
							   BMessenger replyTo)
	: fToken(-1)
{
	InitData(target, message, interval, count, replyTo);
}

// destructor
/*!	\brief Frees all resources associated with the object.
*/
BMessageRunner::~BMessageRunner()
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_UNREGISTER_MESSAGE_RUNNER);
	if (error == B_OK)
		error = request.AddInt32("token", fToken);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = _send_to_roster_(&request, &reply, false);
	// ignore the reply, we can't do anything anyway
}

// InitCheck
/*!	\brief Returns the status of the initialization.

	\note As soon as the last message has been sent, the message runner
		  becomes unusable. InitCheck() will still return \c B_OK, but
		  SetInterval(), SetCount() and GetInfo() will fail.

	\return \c B_OK, if the object is properly initialized, an error code
			otherwise.
*/
status_t
BMessageRunner::InitCheck() const
{
	return (fToken >= 0 ? B_OK : fToken);
}

// SetInterval
/*!	\brief Sets the interval of time between messages.
	\param interval The new interval in microseconds.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_INIT: The message runner is not properly initialized.
	- \c B_BAD_VALUE: \a interval is \c 0 or negative, or the message runner
	  has already sent all messages to be sent and has become unusable.
*/
status_t
BMessageRunner::SetInterval(bigtime_t interval)
{
	return SetParams(true, interval, false, 0);
}

// SetCount
/*!	\brief Sets the number of times message shall be sent.
	\param count Specifies how many times the message shall be sent.
		   A value less than \c 0 for an unlimited number of repetitions.
	- \c B_BAD_VALUE: The message runner has already sent all messages to be
	  sent and has become unusable.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_INIT: The message runner is not properly initialized.
*/
status_t
BMessageRunner::SetCount(int32 count)
{
	return SetParams(false, 0, true, count);
}

// GetInfo
/*!	\brief Returns the time interval between two messages and the number of
		   times the message has still to be sent.
	\param interval Pointer to a pre-allocated bigtime_t variable to be set
		   to the time interval.
	\param count Pointer to a pre-allocated int32 variable to be set
		   to the number of times the message has still to be sent.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: The message runner is not longer valid. All the
	  messages that had to be sent have already been sent.
*/
status_t
BMessageRunner::GetInfo(bigtime_t *interval, int32 *count) const
{
	status_t error =  (fToken >= 0 ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_MESSAGE_RUNNER_INFO);
	if (error == B_OK)
		error = request.AddInt32("token", fToken);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = _send_to_roster_(&request, &reply, false);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			// count
			int32 _count;
			if (reply.FindInt32("count", &_count) == B_OK) {
				if (count)
					*count = _count;
			} else
				error = B_ERROR;
			// interval
			bigtime_t _interval;
			if (reply.FindInt64("interval", &_interval) == B_OK) {
				if (interval)
					*interval = _interval;
			} else
				error = B_ERROR;
		} else
			reply.FindInt32("error", &error);
	}
	return error;
}

// FBC
void BMessageRunner::_ReservedMessageRunner1() {}
void BMessageRunner::_ReservedMessageRunner2() {}
void BMessageRunner::_ReservedMessageRunner3() {}
void BMessageRunner::_ReservedMessageRunner4() {}
void BMessageRunner::_ReservedMessageRunner5() {}
void BMessageRunner::_ReservedMessageRunner6() {}

// copy constructor
/*!	\brief Privatized copy constructor to prevent usage.
*/
BMessageRunner::BMessageRunner(const BMessageRunner &)
	: fToken(-1)
{
}

// =
/*!	\brief Privatized assignment operator to prevent usage.
*/
BMessageRunner &
BMessageRunner::operator=(const BMessageRunner &)
{
	return *this;
}

// InitData
/*!	\brief Initializes the BMessageRunner.

	The success of the initialization can (and should) be asked for via
	InitCheck().

	\note As soon as the last message has been sent, the message runner
		  becomes unusable. InitCheck() will still return \c B_OK, but
		  SetInterval(), SetCount() and GetInfo() will fail.

	\param target Target of the message(s).
	\param message The message to be sent to the target.
	\param interval Period of time before the first message is sent and
		   between messages (if more than one shall be sent) in microseconds.
	\param count Specifies how many times the message shall be sent.
		   A value less than \c 0 for an unlimited number of repetitions.
	\param replyTo Target replies to the delivered message(s) shall be sent to.
*/
void
BMessageRunner::InitData(BMessenger target, const BMessage *message,
						 bigtime_t interval, int32 count, BMessenger replyTo)
{
	status_t error = (message ? B_OK : B_BAD_VALUE);
	if (error == B_OK && count == 0)
		error = B_ERROR;
	// compose the request message
	BMessage request(B_REG_REGISTER_MESSAGE_RUNNER);
	if (error == B_OK)
		error = request.AddInt32("team", BPrivate::current_team());
	if (error == B_OK)
		error = request.AddMessenger("target", target);
	if (error == B_OK)
		error = request.AddMessage("message", message);
	if (error == B_OK)
		error = request.AddInt64("interval", interval);
	if (error == B_OK)
		error = request.AddInt32("count", count);
	if (error == B_OK)
		error = request.AddMessenger("reply_target", replyTo);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = _send_to_roster_(&request, &reply, false);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (reply.FindInt32("token", &fToken) != B_OK)
				error = B_ERROR;
		} else
			reply.FindInt32("error", &error);
	}
	if (error != B_OK)
		fToken = error;
}

// SetParams
status_t
BMessageRunner::SetParams(bool resetInterval, bigtime_t interval,
						  bool resetCount, int32 count)
{
	status_t error = ((resetInterval || resetCount) && fToken >= 0
					  ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_SET_MESSAGE_RUNNER_PARAMS);
	if (error == B_OK)
		error = request.AddInt32("token", fToken);
	if (error == B_OK && resetInterval)
		error = request.AddInt64("interval", interval);
	if (error == B_OK && resetCount)
		error = request.AddInt32("count", count);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = _send_to_roster_(&request, &reply, false);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what != B_REG_SUCCESS)
			reply.FindInt32("error", &error);
	}
	return error;
}

