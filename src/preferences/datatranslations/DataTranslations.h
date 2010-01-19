/*
 * Copyright 2002-2010, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Oliver Siebenmarck
 *		Andrew McCall, mccall@digitalparadise.co.uk
 *		Michael Wilber
 */
#ifndef DATA_TRANSLATIONS_H
#define DATA_TRANSLATIONS_H


#include <Application.h>

#include "DataTranslationsSettings.h"

class BDirectory;
class BEntry;

class DataTranslationsApplication : public BApplication {
public:
								DataTranslationsApplication();
	virtual						~DataTranslationsApplication();

	virtual void				RefsReceived(BMessage* message);
	virtual void				AboutRequested();

			BPoint				WindowCorner() const {
									return fSettings.WindowCorner();
								}
			void				SetWindowCorner(const BPoint& leftTop);

private:
			void				_InstallError(const char* name, status_t status);
			status_t			_Install(BDirectory& target, BEntry& entry);
			void				_NoTranslatorError(const char* name);

private:
			DataTranslationsSettings	fSettings;
};

#endif	// DATA_TRANSLATIONS_H
