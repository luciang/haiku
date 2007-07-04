/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "Settings.h"

#include <Directory.h>
#include <FindDirectory.h>
#include <fs_interface.h>
#include <Path.h>
#include <PathMonitor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct settings_template {
	uint32		type;
	const char*	name;
	const settings_template* sub_template;
	bool		parent_value;
};

// Interface templates

const static settings_template kInterfaceAddressTemplate[] = {
	{B_STRING_TYPE, "family", NULL, true},
	{B_STRING_TYPE, "address", NULL},
	{B_STRING_TYPE, "mask", NULL},
	{B_STRING_TYPE, "peer", NULL},
	{B_STRING_TYPE, "broadcast", NULL},
	{B_STRING_TYPE, "gateway", NULL},
	{B_BOOL_TYPE, "auto config", NULL},
	{0, NULL, NULL}
};

const static settings_template kInterfaceTemplate[] = {
	{B_STRING_TYPE, "device", NULL, true},
	{B_MESSAGE_TYPE, "address", kInterfaceAddressTemplate},
	{B_INT32_TYPE, "flags", NULL},
	{B_INT32_TYPE, "metric", NULL},
	{B_INT32_TYPE, "mtu", NULL},
	{0, NULL, NULL}
};

const static settings_template kInterfacesTemplate[] = {
	{B_MESSAGE_TYPE, "interface", kInterfaceTemplate},
	{0, NULL, NULL}
};

// Service templates

const static settings_template kServiceAddressTemplate[] = {
	{B_STRING_TYPE, "family", NULL, true},
	{B_STRING_TYPE, "type", NULL},
	{B_STRING_TYPE, "protocol", NULL},
	{B_STRING_TYPE, "address", NULL},
	{B_INT32_TYPE, "port", NULL},
	{0, NULL, NULL}
};

const static settings_template kServiceTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_MESSAGE_TYPE, "address", kServiceAddressTemplate},
	{B_STRING_TYPE, "user", NULL},
	{B_STRING_TYPE, "group", NULL},
	{B_STRING_TYPE, "launch", NULL},
	{B_STRING_TYPE, "family", NULL},
	{B_STRING_TYPE, "type", NULL},
	{B_STRING_TYPE, "protocol", NULL},
	{B_INT32_TYPE, "port", NULL},
	{0, NULL, NULL}
};

const static settings_template kServicesTemplate[] = {
	{B_MESSAGE_TYPE, "service", kServiceTemplate},
	{0, NULL, NULL}
};


Settings::Settings()
	:
	fUpdated(false)
{
	_Load();
}


Settings::~Settings()
{
	// only save the settings if something has changed
	if (!fUpdated)
		return;

#if 0
	BFile file;
	if (_Open(&file, B_CREATE_FILE | B_WRITE_ONLY) != B_OK)
		return;
#endif
}


status_t
Settings::_GetPath(const char* name, BPath& path)
{
	if (find_directory(B_COMMON_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;

	path.Append("network");
	create_directory(path.Path(), 0755);

	if (name != NULL)
		path.Append(name);
	return B_OK;
}


const settings_template*
Settings::_FindSettingsTemplate(const settings_template* settingsTemplate,
	const char* name)
{
	while (settingsTemplate->name != NULL) {
		if (!strcmp(name, settingsTemplate->name))
			return settingsTemplate;

		settingsTemplate++;
	}

	return NULL;
}


const settings_template*
Settings::_FindParentValueTemplate(const settings_template* settingsTemplate)
{
	settingsTemplate = settingsTemplate->sub_template;
	if (settingsTemplate == NULL)
		return NULL;

	while (settingsTemplate->name != NULL) {
		if (settingsTemplate->parent_value)
			return settingsTemplate;

		settingsTemplate++;
	}

	return NULL;
}


status_t
Settings::_AddParameter(const driver_parameter& parameter, const char* name,
	uint32 type, BMessage& message)
{
	for (int32 i = 0; i < parameter.value_count; i++) {
		switch (type) {
			case B_STRING_TYPE:
				message.AddString(name, parameter.values[i]);
				break;
			case B_INT32_TYPE:
				message.AddInt32(name, atoi(parameter.values[i]));
				break;
			case B_BOOL_TYPE:
				if (!strcasecmp(parameter.values[i], "true")
					|| !strcasecmp(parameter.values[i], "on")
					|| !strcasecmp(parameter.values[i], "enabled")
					|| !strcasecmp(parameter.values[i], "1"))
					message.AddBool(name, true);
				else
					message.AddBool(name, false);
				break;
		}
	}

	return B_OK;
}


status_t
Settings::_ConvertFromDriverParameter(const driver_parameter& parameter,
	const settings_template* settingsTemplate, BMessage& message)
{
	settingsTemplate = _FindSettingsTemplate(settingsTemplate, parameter.name);
	if (settingsTemplate == NULL) {
		fprintf(stderr, "unknown parameter %s\n", parameter.name);
		return B_BAD_VALUE;
	}

	_AddParameter(parameter, parameter.name, settingsTemplate->type, message);

	if (settingsTemplate->type == B_MESSAGE_TYPE
		&& parameter.parameter_count > 0) {
		status_t status = B_OK;
		BMessage subMessage;
		for (int32 j = 0; j < parameter.parameter_count; j++) {
			status = _ConvertFromDriverParameter(parameter.parameters[j],
				settingsTemplate->sub_template, subMessage);
			if (status < B_OK)
				break;

			const settings_template* parentValueTemplate
				= _FindParentValueTemplate(settingsTemplate);
			if (parentValueTemplate != NULL) {
				_AddParameter(parameter, parentValueTemplate->name,
					parentValueTemplate->type, subMessage);
			}
		}
		if (status == B_OK)
			message.AddMessage(parameter.name, &subMessage);
	}

	return B_OK;
}


status_t
Settings::_ConvertFromDriverSettings(const driver_settings& settings,
	const settings_template* settingsTemplate, BMessage& message)
{
	message.MakeEmpty();

	for (int32 i = 0; i < settings.parameter_count; i++) {
		status_t status = _ConvertFromDriverParameter(settings.parameters[i],
			settingsTemplate, message);
		if (status == B_BAD_VALUE) {
			// ignore unknown entries
			continue;
		}
		if (status < B_OK)
			return status;
	}

	return B_OK;
}


status_t
Settings::_ConvertFromDriverSettings(const char* name,
	const settings_template* settingsTemplate, BMessage& message)
{
	BPath path;
	status_t status = _GetPath(name, path);
	if (status < B_OK)
		return status;

	void* handle = load_driver_settings(path.Path());
	if (handle == NULL)
		return B_ENTRY_NOT_FOUND;

	const driver_settings* settings = get_driver_settings(handle);
	if (settings != NULL)
		status = _ConvertFromDriverSettings(*settings, settingsTemplate, message);

	unload_driver_settings(handle);
	return status;
}


status_t
Settings::_Load(const char* name, uint32* _type)
{
	status_t status = B_ENTRY_NOT_FOUND;

	if (name == NULL || !strcmp(name, "interfaces")) {
		status = _ConvertFromDriverSettings("interfaces", kInterfacesTemplate,
			fInterfaces);
		if (status == B_OK && _type != NULL)
			*_type = kMsgInterfaceSettingsUpdated;
	}
	if (name == NULL || !strcmp(name, "services")) {
		status = _ConvertFromDriverSettings("services", kServicesTemplate,
			fServices);
		if (status == B_OK && _type != NULL)
			*_type = kMsgServiceSettingsUpdated;
	}

	return status;
}


status_t
Settings::_StartWatching(const char* name, const BMessenger& target)
{
	BPath path;
	status_t status = _GetPath(name, path);
	if (status < B_OK)
		return status;

	return BPrivate::BPathMonitor::StartWatching(path.Path(), B_WATCH_STAT,
		target);
}


status_t
Settings::StartMonitoring(const BMessenger& target)
{
	if (_IsWatching(target))
		return B_OK;
	if (_IsWatching())
		StopMonitoring(fListener);

	fListener = target;

	status_t status = _StartWatching("interfaces", target);
	if (status == B_OK)
		status = _StartWatching("services", target);

	return status;
}


status_t
Settings::StopMonitoring(const BMessenger& target)
{
	// TODO: this needs to be changed in case the server will watch
	//	anything else but settings
	return BPrivate::BPathMonitor::StopWatching(target);
}


status_t
Settings::Update(BMessage* message)
{
	message->PrintToStream();
	const char* pathName;
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) < B_OK
		|| message->FindString("path", &pathName) < B_OK)
		return B_BAD_VALUE;

	if (message->FindBool("removed")) {
		// for now, we only consider existing settings files
		// (ie. deleting "services" won't stop any)
		return B_OK;
	}

	int32 fields;
	if (opcode == B_STAT_CHANGED
		&& message->FindInt32("fields", &fields) == B_OK
		&& (fields & FS_WRITE_STAT_MTIME) == 0) {
		// only update when the modified time has changed
		return B_OK;
	}

	BPath path(pathName);
	uint32 type;
	if (_Load(path.Leaf(), &type) == B_OK) {
		BMessage update(type);
		fListener.SendMessage(&update);
	}

	return B_OK;
}


status_t
Settings::GetNextInterface(uint32& cookie, BMessage& interface)
{
	status_t status = fInterfaces.FindMessage("interface", cookie, &interface);
	if (status < B_OK)
		return status;

	cookie++;
	return B_OK;
}


status_t
Settings::GetNextService(uint32& cookie, BMessage& service)
{
	status_t status = fServices.FindMessage("service", cookie, &service);
	if (status < B_OK)
		return status;

	cookie++;
	return B_OK;
}


const BMessage&
Settings::Services() const
{
	return fServices;
}

