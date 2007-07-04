/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SETTINGS_H
#define SETTINGS_H


#include <driver_settings.h>
#include <Message.h>
#include <Messenger.h>


class BPath;
struct settings_template;


class Settings {
	public:
		Settings();
		~Settings();

		status_t GetNextInterface(uint32& cookie, BMessage& interface);
		status_t GetNextService(uint32& cookie, BMessage& service);
		const BMessage& Services() const;

		status_t StartMonitoring(const BMessenger& target);
		status_t StopMonitoring(const BMessenger& target);

		status_t Update(BMessage* message);

	private:
		status_t _Load(const char* name = NULL, uint32* _type = NULL);
		status_t _GetPath(const char* name, BPath& path);

		status_t _StartWatching(const char* name, const BMessenger& target);
		const settings_template* _FindSettingsTemplate(
			const settings_template* settingsTemplate, const char* name);
		const settings_template* _FindParentValueTemplate(
			const settings_template* settingsTemplate);
		status_t _AddParameter(const driver_parameter& parameter,
			const char* name, uint32 type, BMessage& message);

		status_t _ConvertFromDriverParameter(const driver_parameter& parameter,
			const settings_template* settingsTemplate, BMessage& message);
		status_t _ConvertFromDriverSettings(const driver_settings& settings,
			const settings_template* settingsTemplate, BMessage& message);
		status_t _ConvertFromDriverSettings(const char* path,
			const settings_template* settingsTemplate, BMessage& message);

		bool _IsWatching(const BMessenger& target) const { return fListener == target; }
		bool _IsWatching() const { return fListener.IsValid(); }

		BMessenger	fListener;
		BMessage	fInterfaces;
		BMessage	fServices;
		bool		fUpdated;
};

static const uint32 kMsgInterfaceSettingsUpdated = 'SUif';
static const uint32 kMsgServiceSettingsUpdated = 'SUsv';

#endif	// SETTINGS_H
