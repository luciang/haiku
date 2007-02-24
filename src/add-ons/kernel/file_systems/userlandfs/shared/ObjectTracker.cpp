// ObjectTracker.h

#include <new>
#include <typeinfo>

#include "AutoLocker.h"
#include "Debug.h"
#include "ObjectTracker.h"

static char sTrackerBuffer[sizeof(ObjectTracker)];

// constructor
ObjectTrackable::ObjectTrackable()
{
	ObjectTracker::GetDefault()->AddTrackable(this);
}

// destructor
ObjectTrackable::~ObjectTrackable()
{
	ObjectTracker::GetDefault()->RemoveTrackable(this);
}


// #pragma mark -

// constructor
ObjectTracker::ObjectTracker()
	: fLock("object tracker"),
	  fTrackables()
{
}

// destructor
ObjectTracker::~ObjectTracker()
{
	ObjectTrackable* trackable = fTrackables.GetFirst();
	if (trackable) {
		WARN(("ObjectTracker: WARNING: There are still undeleted objects:\n"));
		for (; trackable; trackable = fTrackables.GetNext(trackable)) {
			WARN(("  trackable: %p: type: `%s'\n", trackable,
				typeid(*trackable).name()));
		}
	}
}

// InitDefault
ObjectTracker*
ObjectTracker::InitDefault()
{
	if (!sTracker)
		sTracker = new(sTrackerBuffer) ObjectTracker;
	return sTracker;
}

// ExitDefault
void
ObjectTracker::ExitDefault()
{
	if (sTracker) {
		sTracker->~ObjectTracker();
		sTracker = NULL;
	}
}

// GetDefault
ObjectTracker*
ObjectTracker::GetDefault()
{
	return sTracker;
}

// AddTrackable
void
ObjectTracker::AddTrackable(ObjectTrackable* trackable)
{
	if (!this)
		return;

	if (trackable) {
		AutoLocker<Locker> _(fLock);
		fTrackables.Insert(trackable);
	}
}

// RemoveTrackable
void
ObjectTracker::RemoveTrackable(ObjectTrackable* trackable)
{
	if (!this)
		return;

	if (trackable) {
		AutoLocker<Locker> _(fLock);
		fTrackables.Remove(trackable);
	}
}

// sTracker
ObjectTracker* ObjectTracker::sTracker = NULL;

