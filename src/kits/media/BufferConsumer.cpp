/***********************************************************************
 * AUTHOR: Marcus Overhagen
 *   FILE: BufferConsumer.cpp
 *  DESCR: 
 ***********************************************************************/
#include <BufferConsumer.h>
#include <BufferProducer.h>
#include <BufferGroup.h>
#include <Buffer.h>
#include <TimeSource.h> //for debugging
#include <malloc.h>
#include "debug.h"
#include "MediaMisc.h"
#include "DataExchange.h"
#include "BufferIdCache.h"

/*************************************************************
 * protected BBufferConsumer
 *************************************************************/

/* virtual */
BBufferConsumer::~BBufferConsumer()
{
	CALLED();
	delete fBufferCache;
	if (fDeleteBufferGroup)
		delete fDeleteBufferGroup;
}


/*************************************************************
 * public BBufferConsumer
 *************************************************************/

media_type
BBufferConsumer::ConsumerType()
{
	CALLED();
	return fConsumerType;
}


/* static */ status_t
BBufferConsumer::RegionToClipData(const BRegion *region,
								  int32 *format,
								  int32 *ioSize,
								  void *data)
{
	CALLED();
	
	status_t rv;
	int count;

	count = *ioSize / sizeof(int16);
	rv = BBufferProducer::clip_region_to_shorts(region, static_cast<int16 *>(data), count, &count);
	*ioSize	= count * sizeof(int16);
	*format = BBufferProducer::B_CLIP_SHORT_RUNS;
	
	return rv;
}

/*************************************************************
 * protected BBufferConsumer
 *************************************************************/

/* explicit */
BBufferConsumer::BBufferConsumer(media_type consumer_type) :
	BMediaNode("called by BBufferConsumer"),
	fConsumerType(consumer_type),
	fBufferCache(new _buffer_id_cache),
	fDeleteBufferGroup(0)
{
	CALLED();
	
	AddNodeKind(B_BUFFER_CONSUMER);
}


/* static */ void
BBufferConsumer::NotifyLateProducer(const media_source &what_source,
									bigtime_t how_much,
									bigtime_t performance_time)
{
	CALLED();
	if (IS_INVALID_SOURCE(what_source))
		return;

	producer_late_notice_received_command command;
	command.source = what_source;
	command.how_much = how_much;
	command.performance_time = performance_time;
	
	SendToPort(what_source.port, PRODUCER_LATE_NOTICE_RECEIVED, &command, sizeof(command));
}


status_t
BBufferConsumer::SetVideoClippingFor(const media_source &output,
									 const media_destination &destination,
									 const int16 *shorts,
									 int32 short_count,
									 const media_video_display_info &display,
									 void *user_data,
									 int32 *change_tag,
									 void *_reserved_)
{
	CALLED();
	if (IS_INVALID_SOURCE(output))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;
	if (short_count > int(B_MEDIA_MESSAGE_SIZE - sizeof(producer_video_clipping_changed_command)) / 2)
		debugger("BBufferConsumer::SetVideoClippingFor short_count too large (8000 limit)\n");
		
	producer_video_clipping_changed_command *command;
	size_t size;
	status_t rv;

	size = sizeof(producer_video_clipping_changed_command) + short_count * sizeof(short);
	command = static_cast<producer_video_clipping_changed_command *>(malloc(size));
	command->source = output;
	command->destination = destination;
	command->display = display;
	command->user_data = user_data;
	command->change_tag = NewChangeTag();
	command->short_count = short_count;
	memcpy(command->shorts, shorts, short_count * sizeof(short));
	if (change_tag != NULL)
		*change_tag = command->change_tag;
	
	rv = SendToPort(output.port, PRODUCER_VIDEO_CLIPPING_CHANGED, command, size);
	free(command);
	return rv;
}


status_t
BBufferConsumer::SetOutputEnabled(const media_source &source,
								  const media_destination &destination,
								  bool enabled,
								  void *user_data,
								  int32 *change_tag,
								  void *_reserved_)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;

	producer_enable_output_command command;
	
	command.source = source;
	command.destination = destination;
	command.enabled = enabled;
	command.user_data = user_data;
	command.change_tag = NewChangeTag();
	if (change_tag != NULL)
		*change_tag = command.change_tag;
	
	return SendToPort(source.port, PRODUCER_ENABLE_OUTPUT, &command, sizeof(command));
}


status_t
BBufferConsumer::RequestFormatChange(const media_source &source,
									 const media_destination &destination,
									 const media_format &to_format,
									 void *user_data,
									 int32 *change_tag,
									 void *_reserved_)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;

	producer_format_change_requested_command command;
	
	command.source = source;
	command.destination = destination;
	command.format = to_format;
	command.user_data = user_data;
	command.change_tag = NewChangeTag();
	if (change_tag != NULL)
		*change_tag = command.change_tag;
	
	return SendToPort(source.port, PRODUCER_FORMAT_CHANGE_REQUESTED, &command, sizeof(command));
}


status_t
BBufferConsumer::RequestAdditionalBuffer(const media_source &source,
										 BBuffer *prev_buffer,
										 void *_reserved)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;

	producer_additional_buffer_requested_command command;
	
	command.source = source;
	command.prev_buffer = prev_buffer->ID();
	command.prev_time = 0;
	command.has_seek_tag = false;
	//command.prev_tag = 
	
	return SendToPort(source.port, PRODUCER_ADDITIONAL_BUFFER_REQUESTED, &command, sizeof(command));
}


status_t
BBufferConsumer::RequestAdditionalBuffer(const media_source &source,
										 bigtime_t start_time,
										 void *_reserved)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;

	producer_additional_buffer_requested_command command;
	
	command.source = source;
	command.prev_buffer = 0;
	command.prev_time = start_time;
	command.has_seek_tag = false;
	//command.prev_tag = 
	
	return SendToPort(source.port, PRODUCER_ADDITIONAL_BUFFER_REQUESTED, &command, sizeof(command));
}


status_t
BBufferConsumer::SetOutputBuffersFor(const media_source &source,
									 const media_destination &destination,
									 BBufferGroup *group,
									 void *user_data,
									 int32 *change_tag,
									 bool will_reclaim,
									 void *_reserved_)
{
	CALLED();

	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;
	
	producer_set_buffer_group_command *command;
	BBuffer **buffers;
	int32 buffer_count;	
	size_t size;
	status_t rv;

	if (group == 0) {
		buffer_count = 0;
	} else {
		if (B_OK != group->CountBuffers(&buffer_count))
			return B_ERROR;
	}
	
	if (buffer_count != 0) {	
		buffers = new BBuffer * [buffer_count];
		if (B_OK != group->GetBufferList(buffer_count, buffers)) {
			delete [] buffers;
			return B_ERROR;
		}
	} else {
		buffers = NULL;
	}

	size = sizeof(producer_set_buffer_group_command) + buffer_count * sizeof(media_buffer_id);
	command = static_cast<producer_set_buffer_group_command *>(malloc(size));
	command->source = source;
	command->destination = destination;
	command->user_data = user_data;
	command->change_tag = NewChangeTag();
	command->buffer_count = buffer_count;
	for (int32 i = 0; i < buffer_count; i++)
		command->buffers[i] = buffers[i]->ID();

	delete [] buffers;

	if (change_tag != NULL)
		*change_tag = command->change_tag;
	
	rv = SendToPort(source.port, PRODUCER_SET_BUFFER_GROUP, command, size);
	free(command);

	if (rv == B_OK) {	
		if (fDeleteBufferGroup) // XXX will leak memory if port write failed
			delete fDeleteBufferGroup;
		fDeleteBufferGroup = will_reclaim ? NULL : group;
	}
	return rv;
}


status_t
BBufferConsumer::SendLatencyChange(const media_source &source,
								   const media_destination &destination,
								   bigtime_t my_new_latency,
								   uint32 flags)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;

	producer_latency_changed_command command;
	
	command.source = source;
	command.destination = destination;
	command.latency = my_new_latency;
	command.flags = flags;
	
	return SendToPort(source.port, PRODUCER_LATENCY_CHANGED, &command, sizeof(command));
}

/*************************************************************
 * protected BBufferConsumer
 *************************************************************/

/* virtual */ status_t
BBufferConsumer::HandleMessage(int32 message,
							   const void *data,
							   size_t size)
{
	INFO("BBufferConsumer::HandleMessage %#lx, node %ld\n", message, ID());
	status_t rv;
	switch (message) {
		case CONSUMER_ACCEPT_FORMAT:
		{
			const consumer_accept_format_request *request = static_cast<const consumer_accept_format_request *>(data);
			consumer_accept_format_reply reply;
			reply.format = request->format;
			rv = AcceptFormat(request->dest, &reply.format);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}

		case CONSUMER_GET_NEXT_INPUT:
		{
			const consumer_get_next_input_request *request = static_cast<const consumer_get_next_input_request *>(data);
			consumer_get_next_input_reply reply;
			reply.cookie = request->cookie;
			rv = GetNextInput(&reply.cookie, &reply.input);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}

		case CONSUMER_DISPOSE_INPUT_COOKIE:
		{
			const consumer_dispose_input_cookie_request *request = static_cast<const consumer_dispose_input_cookie_request *>(data);
			consumer_dispose_input_cookie_reply reply;
			DisposeInputCookie(request->cookie);
			request->SendReply(B_OK, &reply, sizeof(reply));
			return B_OK;
		}

		case CONSUMER_BUFFER_RECEIVED:
		{
			const consumer_buffer_received_command *command = static_cast<const consumer_buffer_received_command *>(data);
			BBuffer *buffer;
			buffer = fBufferCache->GetBuffer(command->buffer);
			buffer->SetHeader(&command->header);
			TRACE("calling BBufferConsumer::BufferReceived buffer %ld at perf %Ld and TimeSource()->Now() is %Ld\n", buffer->Header()->buffer, buffer->Header()->start_time, TimeSource()->Now());
			BufferReceived(buffer);
			return B_OK;
		}

		case CONSUMER_PRODUCER_DATA_STATUS:
		{
			const consumer_producer_data_status_command *command = static_cast<const consumer_producer_data_status_command *>(data);
			ProducerDataStatus(command->for_whom, command->status, command->at_performance_time);
			return B_OK;
		}

		case CONSUMER_GET_LATENCY_FOR:
		{
			const consumer_get_latency_for_request *request = static_cast<const consumer_get_latency_for_request *>(data);
			consumer_get_latency_for_reply reply;
			rv = GetLatencyFor(request->for_whom, &reply.latency, &reply.timesource);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}

		case CONSUMER_CONNECTED:
		{
			const consumer_connected_request *request = static_cast<const consumer_connected_request *>(data);
			consumer_connected_reply reply;
			rv = Connected(request->producer, request->where, request->with_format, &reply.input);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}
				
		case CONSUMER_DISCONNECTED:
		{
			const consumer_disconnected_request *request = static_cast<const consumer_disconnected_request *>(data);
			consumer_disconnected_reply reply;
			Disconnected(request->source, request->destination);
			request->SendReply(B_OK, &reply, sizeof(reply));
			return B_OK;
		}

		case CONSUMER_FORMAT_CHANGED:
		{
			const consumer_format_changed_request *request = static_cast<const consumer_format_changed_request *>(data);
			consumer_format_changed_reply reply;
			rv = FormatChanged(request->producer, request->consumer, request->change_tag, request->format);
			request->SendReply(rv, &reply, sizeof(reply));

			// XXX is this RequestCompleted() correct?
			node_request_completed_command completedcommand;
			completedcommand.info.what = media_request_info::B_FORMAT_CHANGED;
			completedcommand.info.change_tag = request->change_tag;
			completedcommand.info.status = reply.result;
			//completedcommand.info.cookie
			completedcommand.info.user_data = 0;
			completedcommand.info.source = request->producer;
			completedcommand.info.destination = request->consumer;
			completedcommand.info.format = request->format;
			SendToPort(request->consumer.port, NODE_REQUEST_COMPLETED, &completedcommand, sizeof(completedcommand));
			return B_OK;
		}

		case CONSUMER_SEEK_TAG_REQUESTED:
		{
			const consumer_seek_tag_requested_request *request = static_cast<const consumer_seek_tag_requested_request *>(data);
			consumer_seek_tag_requested_reply reply;
			rv = SeekTagRequested(request->destination, request->target_time, request->flags, &reply.seek_tag, &reply.tagged_time, &reply.flags);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}

	};
	return B_ERROR;
}

status_t
BBufferConsumer::SeekTagRequested(const media_destination &destination,
								  bigtime_t in_target_time,
								  uint32 in_flags,
								  media_seek_tag *out_seek_tag,
								  bigtime_t *out_tagged_time,
								  uint32 *out_flags)
{
	CALLED();
	// may be implemented by derived classes
	return B_ERROR;
}

/*************************************************************
 * private BBufferConsumer
 *************************************************************/

/*
not implemented:
BBufferConsumer::BBufferConsumer()
BBufferConsumer::BBufferConsumer(const BBufferConsumer &clone)
BBufferConsumer & BBufferConsumer::operator=(const BBufferConsumer &clone)
*/

/* deprecated function for R4 */
/* static */ status_t
BBufferConsumer::SetVideoClippingFor(const media_source &output,
									 const int16 *shorts,
									 int32 short_count,
									 const media_video_display_info &display,
									 int32 *change_tag)
{
	CALLED();
	if (IS_INVALID_SOURCE(output))
		return B_MEDIA_BAD_SOURCE;
	if (short_count > int(B_MEDIA_MESSAGE_SIZE - sizeof(producer_video_clipping_changed_command)) / 2)
		debugger("BBufferConsumer::SetVideoClippingFor short_count too large (8000 limit)\n");
		
	producer_video_clipping_changed_command *command;
	size_t size;
	status_t rv;

	size = sizeof(producer_video_clipping_changed_command) + short_count * sizeof(short);
	command = static_cast<producer_video_clipping_changed_command *>(malloc(size));
	command->source = output;
	command->destination = media_destination::null;
	command->display = display;
	command->user_data = 0;
	command->change_tag = NewChangeTag();
	command->short_count = short_count;
	memcpy(command->shorts, shorts, short_count * sizeof(short));
	if (change_tag != NULL)
		*change_tag = command->change_tag;
	
	rv = SendToPort(output.port, PRODUCER_VIDEO_CLIPPING_CHANGED, command, size);
	free(command);
	return rv;
}


/* deprecated function for R4 */
/* static */ status_t
BBufferConsumer::RequestFormatChange(const media_source &source,
									 const media_destination &destination,
									 media_format *in_to_format,
									 int32 *change_tag)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;
	if (IS_INVALID_DESTINATION(destination))
		return B_MEDIA_BAD_DESTINATION;

	producer_format_change_requested_command command;
	
	command.source = source;
	command.destination = destination;
	command.format = *in_to_format;
	command.user_data = 0;
	command.change_tag = NewChangeTag();
	if (change_tag != NULL)
		*change_tag = command.change_tag;
	
	return SendToPort(source.port, PRODUCER_FORMAT_CHANGE_REQUESTED, &command, sizeof(command));
}


/* deprecated function for R4 */
/* static */ status_t
BBufferConsumer::SetOutputEnabled(const media_source &source,
								  bool enabled,
								  int32 *change_tag)
{
	CALLED();
	if (IS_INVALID_SOURCE(source))
		return B_MEDIA_BAD_SOURCE;

	producer_enable_output_command command;
	
	command.source = source;
	command.destination = media_destination::null;
	command.enabled = enabled;
	command.user_data = 0;
	command.change_tag = NewChangeTag();
	if (change_tag != NULL)
		*change_tag = command.change_tag;
	
	return SendToPort(source.port, PRODUCER_ENABLE_OUTPUT, &command, sizeof(command));
}


status_t BBufferConsumer::_Reserved_BufferConsumer_0(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_1(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_2(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_3(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_4(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_5(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_6(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_7(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_8(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_9(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_10(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_11(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_12(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_13(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_14(void *) { return B_ERROR; }
status_t BBufferConsumer::_Reserved_BufferConsumer_15(void *) { return B_ERROR; }

