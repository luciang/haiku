/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef TEAM_H
#define TEAM_H

#include <Locker.h>

#include "Image.h"
#include "Thread.h"


class Team : public BLocker {
public:
								Team(team_id teamID);
								~Team();

			status_t			Init();

			team_id				ID() const		{ return fID; }

			const char*			Name() const	{ return fName.String(); }
			void				SetName(const BString& name);

			void				AddThread(Thread* thread);
			status_t			AddThread(const thread_info& threadInfo,
									Thread** _thread = NULL);
			void				RemoveThread(Thread* thread);
			bool				RemoveThread(thread_id threadID);
			Thread*				ThreadByID(thread_id threadID) const;

			void				AddImage(Image* image);
			status_t			AddImage(const image_info& imageInfo,
									Image** _image = NULL);
			void				RemoveImage(Image* image);
			bool				RemoveImage(image_id imageID);
			Image*				ImageByID(image_id imageID) const;

private:
			team_id				fID;
			BString				fName;
			ThreadList			fThreads;
			ImageList			fImages;
};

#endif	// TEAM_H
