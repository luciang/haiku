//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Waldemar Kornewald, Waldemar.Kornewald@web.de
//---------------------------------------------------------------------

#include <KPPPReportManager.h>
#include <LockerHelper.h>

#include <KPPPUtils.h>

#include <new.h>


PPPReportManager::PPPReportManager(BLocker& lock)
	: fLock(lock)
{
}


PPPReportManager::~PPPReportManager()
{
	for(int32 index = 0; index < fReportRequests.CountItems(); index++)
		delete fReportRequests.ItemAt(index);
}


void
PPPReportManager::EnableReports(ppp_report_type type, thread_id thread,
	int32 flags = PPP_NO_FLAGS)
{
	LockerHelper locker(fLock);
	
	ppp_report_request *request = new ppp_report_request;
	request->type = type;
	request->thread = thread;
	request->flags = flags;
	
	fReportRequests.AddItem(request);
}


void
PPPReportManager::DisableReports(ppp_report_type type, thread_id thread)
{
	LockerHelper locker(fLock);
	
	ppp_report_request *request;
	
	for(int32 i = 0; i < fReportRequests.CountItems(); i++) {
		request = fReportRequests.ItemAt(i);
		
		if(request->thread != thread)
			continue;
		
		if(request->type == type || request->type == PPP_ALL_REPORTS)
			fReportRequests.RemoveItem(request);
	}
}


bool
PPPReportManager::DoesReport(ppp_report_type type, thread_id thread)
{
	LockerHelper locker(fLock);
	
	ppp_report_request *request;
	
	for(int32 i = 0; i < fReportRequests.CountItems(); i++) {
		request = fReportRequests.ItemAt(i);
		
		if(request->thread == thread && request->type == type)
			return true;
	}
	
	return false;
}


bool
PPPReportManager::Report(ppp_report_type type, int32 code, void *data, int32 length)
{
	if(length > PPP_REPORT_DATA_LIMIT)
		return false;
	
	if(fReportRequests.CountItems() == 0)
		return true;
	
	if(!data)
		length = 0;
	
	LockerHelper locker(fLock);
	
	status_t result;
	thread_id sender;
	bool acceptable = true;
	
	ppp_report_packet report;
	report.type = type;
	report.code = code;
	report.length = length;
	memcpy(report.data, data, length);
	
	ppp_report_request *request;
	
	for(int32 index = 0; index < fReportRequests.CountItems(); index++) {
		request = fReportRequests.ItemAt(index);
		
		result = send_data_with_timeout(request->thread, PPP_REPORT_CODE, &report,
			sizeof(report), PPP_REPORT_TIMEOUT);
		
		if(result == B_BAD_THREAD_ID || result == B_NO_MEMORY) {
			fReportRequests.RemoveItem(request);
			--index;
			continue;
		} else if(result == B_OK) {
			if(request->flags & PPP_WAIT_FOR_REPLY) {
				if(request->flags & PPP_NO_REPLY_TIMEOUT) {
					sender = -1;
					while(sender != request->thread)
						code = receive_data(&sender, NULL, 0);
					result = B_OK;
				} else {
					sender = -1;
					result = B_OK;
					while(sender != request->thread && result == B_OK)
						result = receive_data_with_timeout(&sender, &code, NULL, 0,
							PPP_REPORT_TIMEOUT);
				}
				
				if(result == B_OK && code != B_OK)
					acceptable = false;
			}
		}
		
		if(request->flags & PPP_REMOVE_AFTER_REPORT) {
			fReportRequests.RemoveItem(request);
			--index;
		}
	}
	
	return acceptable;
}
