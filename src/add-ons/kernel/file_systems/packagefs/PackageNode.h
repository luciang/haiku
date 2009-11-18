/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_NODE_H
#define PACKAGE_NODE_H


#include <sys/stat.h>

#include <Referenceable.h>

#include <util/SinglyLinkedList.h>

#include "PackageNodeAttribute.h"


class Package;
class PackageDirectory;


class PackageNode : public BReferenceable,
	public SinglyLinkedListLinkImpl<PackageNode> {
public:
								PackageNode(Package* package, mode_t mode);
	virtual						~PackageNode();

			Package*			GetPackage() const	{ return fPackage; }
			PackageDirectory*	Parent() const		{ return fParent; }
			const char*			Name() const		{ return fName; }

	virtual	status_t			Init(PackageDirectory* parent,
									const char* name);

	virtual	status_t			VFSInit(dev_t deviceID, ino_t nodeID);
	virtual	void				VFSUninit();

			mode_t				Mode() const			{ return fMode; }

			uid_t				UserID() const			{ return fUserID; }
			void				SetUserID(uid_t id)		{ fUserID = id; }

			gid_t				GroupID() const			{ return fGroupID; }
			void				SetGroupID(gid_t id)	{ fGroupID = id; }

			void				SetModifiedTime(const timespec& time)
									{ fModifiedTime = time; }
			const timespec&		ModifiedTime() const
									{ return fModifiedTime; }

	virtual	off_t				FileSize() const;

			void				AddAttribute(PackageNodeAttribute* attribute);
			void				RemoveAttribute(
									PackageNodeAttribute* attribute);

			const PackageNodeAttributeList& Attributes() const
									{ return fAttributes; }

			PackageNodeAttribute* FindAttribute(const char* name) const;

protected:
			Package*			fPackage;
			PackageDirectory*	fParent;
			char*				fName;
			mode_t				fMode;
			uid_t				fUserID;
			gid_t				fGroupID;
			timespec			fModifiedTime;
			PackageNodeAttributeList fAttributes;
};


typedef SinglyLinkedList<PackageNode> PackageNodeList;


#endif	// PACKAGE_NODE_H
