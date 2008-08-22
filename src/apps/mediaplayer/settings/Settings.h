/*
 * Copyright 2008, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Fredrik Modéen <fredrik@modeen.se>
 */
 
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Locker.h>

#include "Notifier.h"
#include "SettingsMessage.h"

struct mpSettings {
	bool	autostart;
	bool	closeWhenDonePlayingMovie;
	bool	closeWhenDonePlayingSound;
	bool	loopMovie;
	bool	loopSound;
	bool	useOverlays;
	bool	scaleBilinear;
	enum {
		BG_MOVIES_FULL_VOLUME = 0,
		BG_MOVIES_HALF_VLUME = 1,
		BG_MOVIES_MUTED = 2
	};
	uint32	backgroundMovieVolumeMode;

	bool operator!=(const mpSettings& other) const;
};

#define SETTINGS_FILENAME "MediaPlayerSettings"

class Settings : public BLocker, public Notifier {
public:
								Settings(
									const char* filename = SETTINGS_FILENAME);

			void				LoadSettings(mpSettings& settings) const;
			void				SaveSettings(const mpSettings& settings);

	static	mpSettings			CurrentSettings();
	static	Settings*			Default();

private:
			SettingsMessage		fSettingsMessage;
			BList				fListeners;

	static	Settings			sGlobalInstance;
};

#endif  // SETTINGS_H
