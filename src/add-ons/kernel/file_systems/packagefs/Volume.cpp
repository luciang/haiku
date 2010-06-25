/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "Volume.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include <new>

#include <AppDefs.h>
#include <KernelExport.h>
#include <NodeMonitor.h>

#include <AutoDeleter.h>

#include <Notifications.h>

#include "ErrorOutput.h"
#include "FDCloser.h"
#include "PackageEntry.h"
#include "PackageEntryAttribute.h"
#include "PackageReader.h"

#include "DebugSupport.h"
#include "Directory.h"
#include "LeafNode.h"
#include "kernel_interface.h"
#include "PackageDirectory.h"
#include "PackageFile.h"
#include "PackageSymlink.h"


// node ID of the root directory
static const ino_t kRootDirectoryID = 1;


// #pragma mark - Job


struct Volume::Job : DoublyLinkedListLinkImpl<Job> {
	Job(Volume* volume)
		:
		fVolume(volume)
	{
	}

	virtual ~Job()
	{
	}

	virtual void Do() = 0;

protected:
	Volume*	fVolume;
};


// #pragma mark - AddPackageDomainJob


struct Volume::AddPackageDomainJob : Job {
	AddPackageDomainJob(Volume* volume, PackageDomain* domain)
		:
		Job(volume),
		fDomain(domain)
	{
		fDomain->AcquireReference();
	}

	virtual ~AddPackageDomainJob()
	{
		fDomain->ReleaseReference();
	}

	virtual void Do()
	{
		fVolume->_AddPackageDomain(fDomain, true);
		fDomain = NULL;
	}

private:
	PackageDomain*	fDomain;
};


// #pragma mark - DomainDirectoryEventJob


struct Volume::DomainDirectoryEventJob : Job {
	DomainDirectoryEventJob(Volume* volume, PackageDomain* domain)
		:
		Job(volume),
		fDomain(domain),
		fEvent()
	{
		fDomain->AcquireReference();
	}

	virtual ~DomainDirectoryEventJob()
	{
		fDomain->ReleaseReference();
	}

	status_t Init(const KMessage* event)
	{
		RETURN_ERROR(fEvent.SetTo(event->Buffer(), -1,
			KMessage::KMESSAGE_CLONE_BUFFER));
	}

	virtual void Do()
	{
		fVolume->_DomainListenerEventOccurred(fDomain, &fEvent);
	}

private:
	PackageDomain*	fDomain;
	KMessage		fEvent;
};


// #pragma mark - PackageLoaderErrorOutput


struct Volume::PackageLoaderErrorOutput : ErrorOutput {
	PackageLoaderErrorOutput(Package* package)
		:
		fPackage(package)
	{
	}

	virtual void PrintErrorVarArgs(const char* format, va_list args)
	{
// TODO:...
	}

private:
	Package*	fPackage;
};


// #pragma mark - PackageLoaderContentHandler


struct Volume::PackageLoaderContentHandler : PackageContentHandler {
	PackageLoaderContentHandler(Package* package)
		:
		fPackage(package),
		fErrorOccurred(false)
	{
	}

	status_t Init()
	{
		return B_OK;
	}

	virtual status_t HandleEntry(PackageEntry* entry)
	{
		if (fErrorOccurred)
			return B_OK;

		PackageDirectory* parentDir = NULL;
		if (entry->Parent() != NULL) {
			parentDir = dynamic_cast<PackageDirectory*>(
				(PackageNode*)entry->Parent()->UserToken());
			if (parentDir == NULL)
				RETURN_ERROR(B_BAD_DATA);
		}

		status_t error;

		// get the file mode -- filter out write permissions
		mode_t mode = entry->Mode() & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);

		// create the package node
		PackageNode* node;
		if (S_ISREG(mode)) {
			// file
			node = new(std::nothrow) PackageFile(fPackage, mode, entry->Data());
		} else if (S_ISLNK(mode)) {
			// symlink
			PackageSymlink* symlink = new(std::nothrow) PackageSymlink(
				fPackage, mode);
			if (symlink == NULL)
				RETURN_ERROR(B_NO_MEMORY);

			error = symlink->SetSymlinkPath(entry->SymlinkPath());
			if (error != B_OK) {
				delete symlink;
				return error;
			}

			node = symlink;
		} else if (S_ISDIR(mode)) {
			// directory
			node = new(std::nothrow) PackageDirectory(fPackage, mode);
		} else
			RETURN_ERROR(B_BAD_DATA);

		if (node == NULL)
			RETURN_ERROR(B_NO_MEMORY);
		BReference<PackageNode> nodeReference(node, true);

		error = node->Init(parentDir, entry->Name());
		if (error != B_OK)
			RETURN_ERROR(error);

		node->SetModifiedTime(entry->ModifiedTime());

		// add it to the parent directory
		if (parentDir != NULL)
			parentDir->AddChild(node);
		else
			fPackage->AddNode(node);

		entry->SetUserToken(node);

		return B_OK;
	}

	virtual status_t HandleEntryAttribute(PackageEntry* entry,
		PackageEntryAttribute* attribute)
	{
		if (fErrorOccurred)
			return B_OK;

		PackageNode* node = (PackageNode*)entry->UserToken();

		PackageNodeAttribute* nodeAttribute = new(std::nothrow)
			PackageNodeAttribute(node, attribute->Type(), attribute->Data());
		if (nodeAttribute == NULL)
			RETURN_ERROR(B_NO_MEMORY)

		status_t error = nodeAttribute->Init(attribute->Name());
		if (error != B_OK) {
			delete nodeAttribute;
			RETURN_ERROR(error);
		}

		node->AddAttribute(nodeAttribute);

		return B_OK;
	}

	virtual status_t HandleEntryDone(PackageEntry* entry)
	{
		return B_OK;
	}

	virtual void HandleErrorOccurred()
	{
		fErrorOccurred = true;
	}

private:
	Package*	fPackage;
	bool		fErrorOccurred;
};


// #pragma mark - DomainDirectoryListener


struct Volume::DomainDirectoryListener : NotificationListener {
	DomainDirectoryListener(Volume* volume, PackageDomain* domain)
		:
		fVolume(volume),
		fDomain(domain)
	{
	}

	virtual void EventOccurred(NotificationService& service,
		const KMessage* event)
	{
		DomainDirectoryEventJob* job = new(std::nothrow)
			DomainDirectoryEventJob(fVolume, fDomain);
		if (job == NULL || job->Init(event)) {
			delete job;
			return;
		}

		fVolume->_PushJob(job);
	}

private:
	Volume*			fVolume;
	PackageDomain*	fDomain;
};


// #pragma mark - Volume


Volume::Volume(fs_volume* fsVolume)
	:
	fFSVolume(fsVolume),
	fRootDirectory(NULL),
	fPackageLoader(-1),
	fNextNodeID(kRootDirectoryID + 1),
	fTerminating(false)
{
	rw_lock_init(&fLock, "packagefs volume");
	mutex_init(&fJobQueueLock, "packagefs volume job queue");
	fJobQueueCondition.Init(this, "packagefs volume job queue");
}


Volume::~Volume()
{
	_TerminatePackageLoader();

	while (PackageDomain* packageDomain = fPackageDomains.RemoveHead())
		packageDomain->ReleaseReference();

	// remove all nodes from the ID hash table
	Node* node = fNodes.Clear(true);
	while (node != NULL) {
		Node* next = node->IDHashTableNext();
		node->ReleaseReference();
		node = next;
	}

	if (fRootDirectory != NULL)
		fRootDirectory->ReleaseReference();

	mutex_destroy(&fJobQueueLock);
	rw_lock_destroy(&fLock);
}


status_t
Volume::Mount()
{
	// init the node table
	status_t error = fNodes.Init();
	if (error != B_OK)
		RETURN_ERROR(error);

	// create the root node
	fRootDirectory = new(std::nothrow) Directory(kRootDirectoryID);
	if (fRootDirectory == NULL)
		RETURN_ERROR(B_NO_MEMORY);
	fNodes.Insert(fRootDirectory);

	// create default package domains
// TODO: Get them from the mount parameters instead!
	error = _AddInitialPackageDomain("/boot/common/packages");
	if (error != B_OK)
		RETURN_ERROR(error);

	// spawn package loader thread
	fPackageLoader = spawn_kernel_thread(&_PackageLoaderEntry,
		"package loader", B_NORMAL_PRIORITY, this);
	if (fPackageLoader < 0)
		RETURN_ERROR(fPackageLoader);

	// publish the root node
	fRootDirectory->AcquireReference();
	error = PublishVNode(fRootDirectory);
	if (error != B_OK) {
		fRootDirectory->ReleaseReference();
		return error;
	}

	// run the package loader
	resume_thread(fPackageLoader);

	return B_OK;
}


void
Volume::Unmount()
{
	_TerminatePackageLoader();
}


status_t
Volume::GetVNode(ino_t nodeID, Node*& _node)
{
	return get_vnode(fFSVolume, nodeID, (void**)&_node);
}


status_t
Volume::PutVNode(ino_t nodeID)
{
	return put_vnode(fFSVolume, nodeID);
}


status_t
Volume::RemoveVNode(ino_t nodeID)
{
	return remove_vnode(fFSVolume, nodeID);
}


status_t
Volume::PublishVNode(Node* node)
{
	return publish_vnode(fFSVolume, node->ID(), node, &gPackageFSVnodeOps,
		node->Mode() & S_IFMT, 0);
}


status_t
Volume::AddPackageDomain(const char* path)
{
	PackageDomain* packageDomain = new(std::nothrow) PackageDomain;
	if (packageDomain == NULL)
		RETURN_ERROR(B_NO_MEMORY);
	BReference<PackageDomain> packageDomainReference(packageDomain, true);

	status_t error = packageDomain->Init(path);
	if (error != B_OK)
		return error;

	Job* job = new(std::nothrow) AddPackageDomainJob(this, packageDomain);
	if (job == NULL)
		RETURN_ERROR(B_NO_MEMORY);

	_PushJob(job);

	return B_OK;
}


/*static*/ status_t
Volume::_PackageLoaderEntry(void* data)
{
	return ((Volume*)data)->_PackageLoader();
}


status_t
Volume::_PackageLoader()
{
	while (!fTerminating) {
		MutexLocker jobQueueLocker(fJobQueueLock);

		Job* job = fJobQueue.RemoveHead();
		if (job == NULL) {
			// no job yet -- wait for someone notifying us
			ConditionVariableEntry waitEntry;
			fJobQueueCondition.Add(&waitEntry);
			jobQueueLocker.Unlock();
			waitEntry.Wait();
			continue;
		}

		// do the job
		jobQueueLocker.Unlock();
		job->Do();
		delete job;
	}

	return B_OK;
}


void
Volume::_TerminatePackageLoader()
{
	fTerminating = true;

	if (fPackageLoader >= 0) {
		MutexLocker jobQueueLocker(fJobQueueLock);
		fJobQueueCondition.NotifyOne();
		jobQueueLocker.Unlock();

		wait_for_thread(fPackageLoader, NULL);
		fPackageLoader = -1;
	}

	// empty the job queue
	while (Job* job = fJobQueue.RemoveHead())
		delete job;
}


void
Volume::_PushJob(Job* job)
{
	MutexLocker jobQueueLocker(fJobQueueLock);
	fJobQueue.Add(job);
	fJobQueueCondition.NotifyOne();
}


status_t
Volume::_AddInitialPackageDomain(const char* path)
{
	PackageDomain* domain = new(std::nothrow) PackageDomain;
	if (domain == NULL)
		RETURN_ERROR(B_NO_MEMORY);
	BReference<PackageDomain> domainReference(domain, true);

	status_t error = domain->Init(path);
	if (error != B_OK)
		return error;

	return _AddPackageDomain(domain, false);
}


status_t
Volume::_AddPackageDomain(PackageDomain* domain, bool notify)
{
	// create a directory listener
	DomainDirectoryListener* listener = new(std::nothrow)
		DomainDirectoryListener(this, domain);
	if (listener == NULL)
		RETURN_ERROR(B_NO_MEMORY);

	// prepare the package domain
	status_t error = domain->Prepare(listener);
	if (error != B_OK) {
		ERROR("Failed to prepare package domain \"%s\": %s\n",
			domain->Path(), strerror(errno));
		return errno;
	}

	// iterate through the dir and create packages
	DIR* dir = opendir(domain->Path());
	if (dir == NULL) {
		ERROR("Failed to open package domain directory \"%s\": %s\n",
			domain->Path(), strerror(errno));
		return errno;
	}
	CObjectDeleter<DIR, int> dirCloser(dir, closedir);

	while (dirent* entry = readdir(dir)) {
		// skip "." and ".."
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		_DomainEntryCreated(domain, domain->DeviceID(), domain->NodeID(),
			-1, entry->d_name, false, notify);
// TODO: -1 node ID?
	}

	// add the packages to the node tree
	VolumeWriteLocker volumeLocker(this);
	for (PackageHashTable::Iterator it = domain->Packages().GetIterator();
			Package* package = it.Next();) {
		error = _AddPackageContent(package, notify);
		if (error != B_OK) {
// TODO: Remove the already added packages!
			return error;
		}
	}

	fPackageDomains.Add(domain);
	domain->AcquireReference();

	return B_OK;
}


status_t
Volume::_LoadPackage(Package* package)
{
	// open package file
	int fd = package->Open();
	if (fd < 0)
		RETURN_ERROR(fd);
	PackageCloser packageCloser(package);

	// initialize package reader
	PackageLoaderErrorOutput errorOutput(package);
	PackageReader packageReader(&errorOutput);
	status_t error = packageReader.Init(fd, false);
	if (error != B_OK)
		return error;

	// parse content
	PackageLoaderContentHandler handler(package);
	error = handler.Init();
	if (error != B_OK)
		return error;

	error = packageReader.ParseContent(&handler);
	if (error != B_OK)
		return error;

	return B_OK;
}


status_t
Volume::_AddPackageContent(Package* package, bool notify)
{
	for (PackageNodeList::Iterator it = package->Nodes().GetIterator();
			PackageNode* node = it.Next();) {
		status_t error = _AddPackageContentRootNode(package, node, notify);
		if (error != B_OK) {
			_RemovePackageContent(package, node, notify);
			return error;
		}
	}

	return B_OK;
}


void
Volume::_RemovePackageContent(Package* package, PackageNode* endNode,
	bool notify)
{
	PackageNode* node = package->Nodes().Head();
	while (node != NULL) {
		if (node == endNode)
			break;

		PackageNode* nextNode = package->Nodes().GetNext(node);
		_RemovePackageContentRootNode(package, node, NULL, notify);
		node = nextNode;
	}
}


/*!	This method recursively iterates through the descendents of the given
	package root node and adds all package nodes to the node tree in
	pre-order.
	Due to limited kernel stack space we avoid deep recursive function calls
	and rather use the package node stack implied by the tree.
*/
status_t
Volume::_AddPackageContentRootNode(Package* package,
	PackageNode* rootPackageNode, bool notify)
{
	PackageNode* packageNode = rootPackageNode;
	Directory* directory = fRootDirectory;
	directory->WriteLock();

	do {
		Node* node;
		status_t error = _AddPackageNode(directory, packageNode, notify, node);
		if (error != B_OK) {
			// unlock all directories
			while (directory != NULL) {
				directory->WriteUnlock();
				directory = directory->Parent();
			}

			// remove the added package nodes
			_RemovePackageContentRootNode(package, rootPackageNode, packageNode,
				notify);
			return error;
		}

		// recursive into directory
		if (PackageDirectory* packageDirectory
				= dynamic_cast<PackageDirectory*>(packageNode)) {
			if (packageDirectory->FirstChild() != NULL) {
				directory = dynamic_cast<Directory*>(node);
				packageNode = packageDirectory->FirstChild();
				directory->WriteLock();
				continue;
			}
		}

		// continue with the next available (ancestors's) sibling
		do {
			PackageDirectory* packageDirectory = packageNode->Parent();
			PackageNode* sibling = packageDirectory != NULL
				? packageDirectory->NextChild(packageNode) : NULL;

			if (sibling != NULL) {
				packageNode = sibling;
				break;
			}

			// no more siblings -- go back up the tree
			packageNode = packageDirectory;
			directory->WriteUnlock();
			directory = directory->Parent();
				// the parent is still locked, so this is safe
		} while (packageNode != NULL);
	} while (packageNode != NULL);

	return B_OK;
}


/*!	Recursively iterates through the descendents of the given package root node
	and removes all package nodes from the node tree in post-order, until
	encountering \a endPackageNode (if non-null).
	Due to limited kernel stack space we avoid deep recursive function calls
	and rather use the package node stack implied by the tree.
*/
void
Volume::_RemovePackageContentRootNode(Package* package,
	PackageNode* rootPackageNode, PackageNode* endPackageNode, bool notify)
{
	PackageNode* packageNode = rootPackageNode;
	Directory* directory = fRootDirectory;
	directory->WriteLock();

	do {
		if (packageNode == endPackageNode)
			break;

		// recursive into directory
		if (PackageDirectory* packageDirectory
				= dynamic_cast<PackageDirectory*>(packageNode)) {
			if (packageDirectory->FirstChild() != NULL) {
				directory = dynamic_cast<Directory*>(
					directory->FindChild(packageNode->Name()));
				packageNode = packageDirectory->FirstChild();
				directory->WriteLock();
				continue;
			}
		}

		// continue with the next available (ancestors's) sibling
		do {
			PackageDirectory* packageDirectory = packageNode->Parent();
			PackageNode* sibling = packageDirectory != NULL
				? packageDirectory->NextChild(packageNode) : NULL;

			// we're done with the node -- remove it
			_RemovePackageNode(directory, packageNode,
				directory->FindChild(packageNode->Name()), notify);

			if (sibling != NULL) {
				packageNode = sibling;
				break;
			}

			// no more siblings -- go back up the tree
			packageNode = packageDirectory;
			directory->WriteUnlock();
			directory = directory->Parent();
				// the parent is still locked, so this is safe
		} while (packageNode != NULL/* && packageNode != rootPackageNode*/);
	} while (packageNode != NULL/* && packageNode != rootPackageNode*/);
}


status_t
Volume::_AddPackageNode(Directory* directory, PackageNode* packageNode,
	bool notify, Node*& _node)
{
	bool newNode = false;
	Node* node = directory->FindChild(packageNode->Name());
	if (node == NULL) {
		status_t error = _CreateNode(packageNode->Mode(), directory,
			packageNode->Name(), node);
		if (error != B_OK)
			return error;
		newNode = true;
	}
	BReference<Node> nodeReference(node);

	status_t error = node->AddPackageNode(packageNode);
	if (error != B_OK) {
		// remove the node, if created before
		if (newNode)
			_RemoveNode(node);
		return error;
	}

	if (notify) {
		if (newNode) {
			notify_entry_created(ID(), directory->ID(), node->Name(),
				node->ID());
		} else if (packageNode == node->GetPackageNode()) {
			// The new package node has become the one representing the node.
			// Send stat changed notification for directories and entry
			// removed + created notifications for files and symlinks.
			if (S_ISDIR(packageNode->Mode())) {
				notify_stat_changed(ID(), node->ID(),
					B_STAT_MODE | B_STAT_UID | B_STAT_GID | B_STAT_SIZE
						| B_STAT_ACCESS_TIME | B_STAT_MODIFICATION_TIME
						| B_STAT_CREATION_TIME | B_STAT_CHANGE_TIME);
				// TODO: Actually the attributes might change, too!
			} else {
				notify_entry_removed(ID(), directory->ID(), node->Name(),
					node->ID());
				notify_entry_created(ID(), directory->ID(), node->Name(),
					node->ID());
			}
		}
	}

	_node = node;
	return B_OK;
}


void
Volume::_RemovePackageNode(Directory* directory, PackageNode* packageNode,
	Node* node, bool notify)
{
	BReference<Node> nodeReference(node);

	PackageNode* headPackageNode = node->GetPackageNode();
	node->RemovePackageNode(packageNode);

	// If the node doesn't have any more package nodes attached, remove it
	// completely.
	bool nodeRemoved = false;
	if (node->GetPackageNode() == NULL) {
		// we get and put the vnode to notify the VFS
		// TODO: We should probably only do that, if the node is known to the
		// VFS in the first place.
		Node* dummyNode;
		bool gotVNode = GetVNode(node->ID(), dummyNode) == B_OK;

		_RemoveNode(node);
		nodeRemoved = true;

		if (gotVNode) {
			RemoveVNode(node->ID());
			PutVNode(node->ID());
		}
	}

	if (!notify)
		return;

	// send notifications
	if (nodeRemoved) {
		notify_entry_removed(ID(), directory->ID(), node->Name(), node->ID());
	} else if (packageNode == headPackageNode) {
		// The new package node was the one representing the node.
		// Send stat changed notification for directories and entry
		// removed + created notifications for files and symlinks.
		if (S_ISDIR(packageNode->Mode())) {
			notify_stat_changed(ID(), node->ID(),
				B_STAT_MODE | B_STAT_UID | B_STAT_GID | B_STAT_SIZE
					| B_STAT_ACCESS_TIME | B_STAT_MODIFICATION_TIME
					| B_STAT_CREATION_TIME | B_STAT_CHANGE_TIME);
			// TODO: Actually the attributes might change, too!
		} else {
			notify_entry_removed(ID(), directory->ID(), node->Name(),
				node->ID());
			notify_entry_created(ID(), directory->ID(), node->Name(),
				node->ID());
		}
	}
}


status_t
Volume::_CreateNode(mode_t mode, Directory* parent, const char* name,
	Node*& _node)
{
	Node* node;
	if (S_ISREG(mode) || S_ISLNK(mode))
		node = new(std::nothrow) LeafNode(fNextNodeID++);
	else if (S_ISDIR(mode))
		node = new(std::nothrow) Directory(fNextNodeID++);
	else
		RETURN_ERROR(B_UNSUPPORTED);

	if (node == NULL)
		RETURN_ERROR(B_NO_MEMORY);
	BReference<Node> nodeReference(node, true);

	status_t error = node->Init(parent, name);
	if (error != B_OK)
		return error;

	parent->AddChild(node);

	fNodes.Insert(node);
	nodeReference.Detach();
		// we keep the initial node reference for this table

	_node = node;
	return B_OK;
}


void
Volume::_RemoveNode(Node* node)
{
	// remove from parent
	Directory* parent = node->Parent();
	parent->RemoveChild(node);

	// remove from node table
	fNodes.Remove(node);
	node->ReleaseReference();
}


void
Volume::_DomainListenerEventOccurred(PackageDomain* domain,
	const KMessage* event)
{
	int32 opcode;
	if (event->What() != B_NODE_MONITOR
		|| event->FindInt32("opcode", &opcode) != B_OK) {
		return;
	}

	switch (opcode) {
		case B_ENTRY_CREATED:
		{
			int32 device;
			int64 directory;
			int64 node;
			const char* name;
			if (event->FindInt32("device", &device) == B_OK
				&& event->FindInt64("directory", &directory) == B_OK
				&& event->FindInt64("node", &node) == B_OK
				&& event->FindString("name", &name) == B_OK) {
				_DomainEntryCreated(domain, device, directory, node, name,
					true, true);
			}
			break;
		}

		case B_ENTRY_REMOVED:
		{
			int32 device;
			int64 directory;
			int64 node;
			const char* name;
			if (event->FindInt32("device", &device) == B_OK
				&& event->FindInt64("directory", &directory) == B_OK
				&& event->FindInt64("node", &node) == B_OK
				&& event->FindString("name", &name) == B_OK) {
				_DomainEntryRemoved(domain, device, directory, node, name,
					true);
			}
			break;
		}

		case B_ENTRY_MOVED:
		{
			int32 device;
			int64 fromDirectory;
			int64 toDirectory;
			int32 nodeDevice;
			int64 node;
			const char* fromName;
			const char* name;
			if (event->FindInt32("device", &device) == B_OK
				&& event->FindInt64("from directory", &fromDirectory) == B_OK
				&& event->FindInt64("to directory", &toDirectory) == B_OK
				&& event->FindInt32("node device", &nodeDevice) == B_OK
				&& event->FindInt64("node", &node) == B_OK
				&& event->FindString("from name", &fromName) == B_OK
				&& event->FindString("name", &name) == B_OK) {
				_DomainEntryMoved(domain, device, fromDirectory, toDirectory,
					nodeDevice, node, fromName, name, true);
			}
			break;
		}

		default:
			break;
	}
}


void
Volume::_DomainEntryCreated(PackageDomain* domain, dev_t deviceID,
	ino_t directoryID, ino_t nodeID, const char* name, bool addContent,
	bool notify)
{
	// let's see, if things look plausible
	if (deviceID != domain->DeviceID() || directoryID != domain->NodeID()
		|| domain->FindPackage(name) != NULL) {
		return;
	}

	// check whether the entry is a file
	struct stat st;
	if (fstatat(domain->DirectoryFD(), name, &st, AT_SYMLINK_NOFOLLOW) < 0
		|| !S_ISREG(st.st_mode)) {
		return;
	}

	// create a package
	Package* package = new(std::nothrow) Package(domain, st.st_dev, st.st_ino);
	if (package == NULL)
		return;
	BReference<Package> packageReference(package, true);

	status_t error = package->Init(name);
	if (error != B_OK)
		return;

	error = _LoadPackage(package);
	if (error != B_OK)
		return;

	VolumeWriteLocker volumeLocker(this);
	domain->AddPackage(package);

	// add the package to the node tree
	if (addContent) {
		error = _AddPackageContent(package, notify);
		if (error != B_OK) {
			domain->RemovePackage(package);
			return;
		}
	}
}


void
Volume::_DomainEntryRemoved(PackageDomain* domain, dev_t deviceID,
	ino_t directoryID, ino_t nodeID, const char* name, bool notify)
{
	// let's see, if things look plausible
	if (deviceID != domain->DeviceID() || directoryID != domain->NodeID())
		return;

	Package* package = domain->FindPackage(name);
	if (package == NULL)
		return;
	BReference<Package> packageReference(package);

	// remove the package
	VolumeWriteLocker volumeLocker(this);
	_RemovePackageContent(package, NULL, true);
	domain->RemovePackage(package);
}


void
Volume::_DomainEntryMoved(PackageDomain* domain, dev_t deviceID,
	ino_t fromDirectoryID, ino_t toDirectoryID, dev_t nodeDeviceID,
	ino_t nodeID, const char* fromName, const char* name, bool notify)
{
	_DomainEntryRemoved(domain, deviceID, fromDirectoryID, nodeID, fromName,
		notify);
	_DomainEntryCreated(domain, deviceID, toDirectoryID, nodeID, name, true,
		notify);
}
