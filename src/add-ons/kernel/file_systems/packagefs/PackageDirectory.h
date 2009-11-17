/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_DIRECTORY_H
#define PACKAGE_DIRECTORY_H


#include "PackageNode.h"


class PackageDirectory : public PackageNode {
public:
								PackageDirectory();
	virtual						~PackageDirectory();

			void				AddChild(PackageNode* node);
			void				RemoveChild(PackageNode* node);

	inline	PackageNode*		FirstChild() const;
	inline	PackageNode*		NextChild(PackageNode* node) const;

			const PackageNodeList& Children() const
									{ return fChildren; }

private:
			PackageNodeList		fChildren;
};


PackageNode*
PackageDirectory::FirstChild() const
{
	return fChildren.First();
}


PackageNode*
PackageDirectory::NextChild(PackageNode* node) const
{
	return fChildren.GetNext(node);
}


#endif	// PACKAGE_DIRECTORY_H
