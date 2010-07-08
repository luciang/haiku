/*
 * Copyright 2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ARCHIVING_MANAGERS_H
#define _ARCHIVING_MANAGERS_H


#include <map>

#include <String.h>
#include <ObjectList.h>
#include <MessagePrivate.h>

#include <Archivable.h>

namespace BPrivate {
namespace Archiving {

extern const char* kArchiveCountField;
extern const char* kArchivableField;
extern const char* kTokenField;

class BManagerBase {
public:
	enum manager_type {
		ARCHIVE_MANAGER,
		UNARCHIVE_MANAGER
	};

	BManagerBase(BMessage* topLevelArchive, manager_type type)
		:
		fTopLevelArchive(topLevelArchive),
		fType(type)
	{
		MarkArchive(topLevelArchive);
	}


	static BManagerBase*
	ManagerPointer(const BMessage* constArchive)
	{
		BMessage* archive = const_cast<BMessage*>(constArchive);

		return static_cast<BManagerBase*>(
			BMessage::Private(archive).ArchivingPointer());
	}


	static void
	SetManagerPointer(BMessage* archive, BManagerBase* manager)
	{
		BMessage::Private(archive).SetArchivingPointer(manager);
	}


	void
	MarkArchive(BMessage* archive)
	{
		BManagerBase* manager = ManagerPointer(archive);
		if (manager != NULL)
			debugger("Overlapping managed archiving/unarchiving sessions!");

		SetManagerPointer(archive, this);
	}


	void
	UnmarkArchive(BMessage* archive)
	{
		BManagerBase* manager = ManagerPointer(archive);
		if (manager == this)
			SetManagerPointer(archive, NULL);
		else
			debugger("Overlapping managed archiving/unarchiving sessions!");
	}


	static	BArchiveManager*	ArchiveManager(const BMessage* archive);
	static 	BUnarchiveManager*	UnarchiveManager(const BMessage* archive);

protected:

	~BManagerBase()
	{
		UnmarkArchive(fTopLevelArchive);
	}
			BMessage*			fTopLevelArchive;
			manager_type		fType;
};


class BArchiveManager: public BManagerBase {
public:
						BArchiveManager(const BArchiver* creator);

			status_t	GetTokenForArchivable(BArchivable* archivable,
							int32& _token);

			status_t	ArchiveObject(BArchivable* archivable, bool deep);

			bool		IsArchived(BArchivable* archivable);

			status_t	ArchiverLeaving(const BArchiver* archiver);
			void		Acquire();
			void		RegisterArchivable(const BArchivable* archivable);

private:
						~BArchiveManager();

			struct ArchiveInfo;
			typedef std::map<const BArchivable*, ArchiveInfo> TokenMap;

			TokenMap			fTokenMap;
			const BArchiver*	fCreator;
};




class BUnarchiveManager: public BManagerBase {
public:
					BUnarchiveManager(BMessage* topLevelArchive);

	status_t		ArchivableForToken(BArchivable** archivable,
						int32 token);

	bool			IsInstantiated(int32 token);

	void			RegisterArchivable(BArchivable* archivable);
	status_t		UnarchiverLeaving(const BUnarchiver* archiver);
	void			Acquire();

private:
					~BUnarchiveManager();
	status_t		_ExtractArchiveAt(int32 index);
	status_t		_InstantiateObjectForToken(int32 token);

	struct ArchiveInfo;
	ArchiveInfo*			fObjects;
	int32					fObjectCount;
	int32					fTokenInProgress;
	int32					fRefCount;
};


} // namespace Archiving
} // namespace BPrivate

#endif
