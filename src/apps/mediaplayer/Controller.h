/*
 * Controller.h - Media Player for the Haiku Operating System
 *
 * Copyright (C) 2006 Marcus Overhagen <marcus@overhagen.de>
 * Copyright (C) 2007-2008 Stephan Aßmus <superstippi@gmx.de> (MIT Ok)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */
#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <Entry.h>
#include <MediaDefs.h>
#include <MediaFormats.h>
#include <MediaNode.h>
#include <List.h>
#include <Locker.h>
#include <String.h>

#include "ListenerAdapter.h"
#include "NodeManager.h"
#include "PlaylistItem.h"

class AudioTrackSupplier;
class BBitmap;
class BMediaFile;
class BMediaTrack;
class PlaylistItem;
class ProxyAudioSupplier;
class ProxyVideoSupplier;
class SoundOutput;
class VideoTrackSupplier;
class VideoView;

class Controller : public NodeManager {
public:

	class Listener {
	public:
								Listener();
		virtual					~Listener();

		virtual	void			FileFinished();
		virtual	void			FileChanged();

		virtual	void			VideoTrackChanged(int32 index);
		virtual	void			AudioTrackChanged(int32 index);

		virtual	void			VideoStatsChanged();
		virtual	void			AudioStatsChanged();

		virtual	void			PlaybackStateChanged(uint32 state);
		virtual	void			PositionChanged(float position);
		virtual	void			VolumeChanged(float volume);
		virtual	void			MutedChanged(bool muted);
	};

								Controller();
	virtual 					~Controller();

	// PlaybackManager interface
	virtual	void				MessageReceived(BMessage* message);
	virtual	int64				Duration();

	// NodeManager interface
	virtual	VideoTarget*		CreateVideoTarget();
	virtual	VideoSupplier*		CreateVideoSupplier();
	virtual	AudioSupplier*		CreateAudioSupplier();

	// Controller
			status_t			SetTo(const PlaylistItemRef& item);
			const PlaylistItem*	Item() const
									{ return fItem.Get(); }
			void				PlayerActivated(bool active);

			void				GetSize(int *width, int *height,
									int* widthAspect = NULL,
									int* heightAspect = NULL);

			int					AudioTrackCount();
			int					VideoTrackCount();

			status_t			SelectAudioTrack(int n);
			int					CurrentAudioTrack();
			status_t			SelectVideoTrack(int n);
			int					CurrentVideoTrack();

			void				Stop();
			void				Play();
			void				Pause();
			void				TogglePlaying();

			uint32				PlaybackState();

			bigtime_t			TimeDuration();
			bigtime_t			TimePosition();

	virtual	void				SetVolume(float factor);
			float				Volume();
			void				VolumeUp();
			void				VolumeDown();
			void				ToggleMute();
			void				SetPosition(float value);

			bool				HasFile();
			status_t			GetFileFormatInfo(
									media_file_format* fileFormat);
			status_t			GetCopyright(BString* copyright);
			status_t			GetLocation(BString* location);
			status_t			GetName(BString* name);
			status_t			GetEncodedVideoFormat(media_format* format);
			status_t			GetVideoCodecInfo(media_codec_info* info);
			status_t			GetEncodedAudioFormat(media_format* format);
			status_t			GetAudioCodecInfo(media_codec_info* info);

	// video view
			void				SetVideoView(VideoView *view);

			bool				IsOverlayActive();

	// notification support
			bool				AddListener(Listener* listener);
			void				RemoveListener(Listener* listener);

private:
			void				_AdoptGlobalSettings();

			uint32				_PlaybackState(int32 playingMode) const;

			void				_NotifyFileChanged() const;
			void				_NotifyFileFinished() const;
			void				_NotifyVideoTrackChanged(int32 index) const;
			void				_NotifyAudioTrackChanged(int32 index) const;

			void				_NotifyVideoStatsChanged() const;
			void				_NotifyAudioStatsChanged() const;

			void				_NotifyPlaybackStateChanged(uint32 state) const;
			void				_NotifyPositionChanged(float position) const;
			void				_NotifyVolumeChanged(float volume) const;
			void				_NotifyMutedChanged(bool muted) const;

	// overridden from PlaybackManager so that we
	// can use our own Listener mechanism
	virtual	void				NotifyPlayModeChanged(int32 mode) const;
	virtual	void				NotifyLoopModeChanged(int32 mode) const;
	virtual	void				NotifyLoopingEnabledChanged(
									bool enabled) const;
	virtual	void				NotifyVideoBoundsChanged(BRect bounds) const;
	virtual	void				NotifyFPSChanged(float fps) const;
	virtual	void				NotifyCurrentFrameChanged(int32 frame) const;
	virtual	void				NotifySpeedChanged(float speed) const;
	virtual	void				NotifyFrameDropped() const;
	virtual	void				NotifyStopFrameReached() const;


			VideoView*			fVideoView;
			float				fVolume;
			float				fActiveVolume;
			bool				fMuted;

			PlaylistItemRef		fItem;
			BMediaFile*			fMediaFile;

			ProxyVideoSupplier*	fVideoSupplier;
			ProxyAudioSupplier*	fAudioSupplier;
			VideoTrackSupplier*	fVideoTrackSupplier;
			AudioTrackSupplier*	fAudioTrackSupplier;

			BList				fAudioTrackList;
			BList				fVideoTrackList;

	mutable	bigtime_t			fPosition;
			bigtime_t			fDuration;
			float				fVideoFrameRate;
	mutable	int32				fSeekFrame;
			bigtime_t			fLastSeekEventTime;

			ListenerAdapter		fGlobalSettingsListener;

			bool				fAutoplaySetting;
				// maintains the auto-play setting
			bool 				fAutoplay;
				// is true if the player is already playing
				// otherwise it's the same as fAutoplaySetting
			bool				fLoopMovies;
			bool				fLoopSounds;
			uint32				fBackgroundMovieVolumeMode;

			BList				fListeners;
};


#endif

