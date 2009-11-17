/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_LEAF_NODE_H
#define PACKAGE_LEAF_NODE_H


#include "PackageNode.h"


class PackageData;


class PackageLeafNode : public PackageNode {
public:
								PackageLeafNode(Package* package, mode_t mode);
	virtual						~PackageLeafNode();

	virtual	const char*			SymlinkPath() const;

	virtual	status_t			Read(off_t offset, void* buffer,
									size_t* bufferSize);

public:
			SinglyLinkedListLink<PackageLeafNode> fListLink;
};


typedef SinglyLinkedList<PackageLeafNode,
	SinglyLinkedListMemberGetLink<PackageLeafNode,
		&PackageLeafNode::fListLink> > PackageLeafNodeList;


#endif	// PACKAGE_LEAF_NODE_H
