/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "TeamSettings.h"

#include <new>

#include <Message.h>

#include <AutoLocker.h>

#include "ArchivingUtils.h"
#include "BreakpointSetting.h"
#include "Team.h"
#include "UserBreakpoint.h"


TeamSettings::TeamSettings()
{
}


TeamSettings::TeamSettings(const TeamSettings& other)
{
	try {
		*this = other;
	} catch (...) {
		_Unset();
		throw;
	}
}


TeamSettings::~TeamSettings()
{
	_Unset();
}


status_t
TeamSettings::SetTo(Team* team)
{
	_Unset();

	AutoLocker<Team> locker(team);

	fTeamName = team->Name();

	// add breakpoints
	for (UserBreakpointList::ConstIterator it
			= team->UserBreakpoints().GetIterator();
		UserBreakpoint* breakpoint = it.Next();) {
		BreakpointSetting* breakpointSetting
			= new(std::nothrow) BreakpointSetting;
		if (breakpointSetting == NULL)
			return B_NO_MEMORY;

		status_t error = breakpointSetting->SetTo(breakpoint->Location(),
			breakpoint->IsEnabled());
		if (error == B_OK && !fBreakpoints.AddItem(breakpointSetting))
			error = B_NO_MEMORY;
		if (error != B_OK) {
			delete breakpointSetting;
			return error;
		}
	}

	return B_OK;
}


status_t
TeamSettings::SetTo(const BMessage& archive)
{
	_Unset();

	status_t error = archive.FindString("teamName", &fTeamName);
	if (error != B_OK)
		return error;

	// add breakpoints
	BMessage childArchive;
	for (int32 i = 0; archive.FindMessage("breakpoints", i, &childArchive)
			== B_OK; i++) {
		BreakpointSetting* breakpointSetting
			= new(std::nothrow) BreakpointSetting;
		if (breakpointSetting == NULL)
			return B_NO_MEMORY;

		error = breakpointSetting->SetTo(childArchive);
		if (error == B_OK && !fBreakpoints.AddItem(breakpointSetting))
			error = B_NO_MEMORY;
		if (error != B_OK) {
			delete breakpointSetting;
			return error;
		}
	}

	return B_OK;
}


status_t
TeamSettings::WriteTo(BMessage& archive) const
{
	status_t error = archive.AddString("teamName", fTeamName);
	if (error != B_OK)
		return error;

	for (int32 i = 0; BreakpointSetting* breakpoint = fBreakpoints.ItemAt(i);
			i++) {
		BMessage childArchive;
		error = breakpoint->WriteTo(childArchive);
		if (error != B_OK)
			return error;

		error = archive.AddMessage("breakpoints", &childArchive);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


int32
TeamSettings::CountBreakpoints() const
{
	return fBreakpoints.CountItems();
}


const BreakpointSetting*
TeamSettings::BreakpointAt(int32 index) const
{
	return fBreakpoints.ItemAt(index);
}


TeamSettings&
TeamSettings::operator=(const TeamSettings& other)
{
	if (this == &other)
		return *this;

	_Unset();

	fTeamName = other.fTeamName;

	for (int32 i = 0; BreakpointSetting* breakpoint
			= other.fBreakpoints.ItemAt(i); i++) {
		BreakpointSetting* clonedBreakpoint
			= new BreakpointSetting(*breakpoint);
		if (!fBreakpoints.AddItem(clonedBreakpoint)) {
			delete clonedBreakpoint;
			throw std::bad_alloc();
		}
	}

	return *this;
}


void
TeamSettings::_Unset()
{
	for (int32 i = 0; BreakpointSetting* breakpoint = fBreakpoints.ItemAt(i);
			i++) {
		delete breakpoint;
	}
	fBreakpoints.MakeEmpty();

	fTeamName.Truncate(0);
}
