/*
 * Copyright 2002-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NODE_H
#define _NODE_H


#include <Statable.h>

class BDirectory;
class BEntry;
class BString;
struct entry_ref;

//! Reference structure to a particular vnode on a particular device
/*! <b>node_ref</b> - A node reference.

	@author <a href="mailto:tylerdauwalder@users.sf.net">Tyler Dauwalder</a>
	@author Be Inc.
	@version 0.0.0
*/
struct node_ref {
	node_ref();
	node_ref(const node_ref &ref);

	bool operator==(const node_ref &ref) const;
	bool operator!=(const node_ref &ref) const;
	node_ref& operator=(const node_ref &ref);

	bool operator<(const node_ref &ref) const
	{
		return device < ref.device
			|| (device == ref.device && node < ref.node);
	}

	dev_t device;
	ino_t node;
};


//! A BNode represents a chunk of data in the filesystem.
/*! The BNode class provides an interface for manipulating the data and attributes
	belonging to filesystem entries. The BNode is unaware of the name that refers
	to it in the filesystem (i.e. its entry); a BNode is solely concerned with
	the entry's data and attributes.


	@author <a href='mailto:tylerdauwalder@users.sf.net'>Tyler Dauwalder</a>
	@version 0.0.0

*/
class BNode : public BStatable {
public:
	BNode();
	BNode(const entry_ref *ref);
	BNode(const BEntry *entry);
	BNode(const char *path);
	BNode(const BDirectory *dir, const char *path);
	BNode(const BNode &node);
	virtual ~BNode();

	status_t InitCheck() const;

	virtual status_t GetStat(struct stat *st) const;

	status_t SetTo(const entry_ref *ref);
	status_t SetTo(const BEntry *entry);
	status_t SetTo(const char *path);
	status_t SetTo(const BDirectory *dir, const char *path);
	void Unset();

	status_t Lock();
	status_t Unlock();

	status_t Sync();

	ssize_t WriteAttr(const char *name, type_code type, off_t offset,
		const void *buffer, size_t len);
	ssize_t ReadAttr(const char *name, type_code type, off_t offset,
		void *buffer, size_t len) const;
	status_t RemoveAttr(const char *name);
	status_t RenameAttr(const char *oldname, const char *newname);
	status_t GetAttrInfo(const char *name, struct attr_info *info) const;
	status_t GetNextAttrName(char *buffer);
	status_t RewindAttrs();
	status_t WriteAttrString(const char *name, const BString *data);
	status_t ReadAttrString(const char *name, BString *result) const;

	BNode& operator=(const BNode &node);
	bool operator==(const BNode &node) const;
	bool operator!=(const BNode &node) const;

	int Dup();  // This should be "const" but R5's is not... Ugggh.

private:
	friend class BFile;
	friend class BDirectory;
	friend class BSymLink;

	virtual void _RudeNode1();
	virtual void _RudeNode2();
	virtual void _RudeNode3();
	virtual void _RudeNode4();
	virtual void _RudeNode5();
	virtual void _RudeNode6();

	uint32 rudeData[4];

private:
	status_t set_fd(int fd);
	virtual void close_fd();
	void set_status(status_t newStatus);

	status_t _SetTo(int fd, const char *path, bool traverse);
	status_t _SetTo(const entry_ref *ref, bool traverse);

	virtual status_t set_stat(struct stat &st, uint32 what);

	int fFd;
	int fAttrFd;
	status_t fCStatus;

	status_t InitAttrDir();
};

#endif	// _NODE_H
