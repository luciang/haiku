/*
 * Copyright 2006-2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef REMOVE_STYLES_COMMAND_H
#define REMOVE_STYLES_COMMAND_H


#include "Command.h"

#include <List.h>


namespace BPrivate {
namespace Icon {
	class Style;
	class StyleContainer;
}
}
using namespace BPrivate::Icon;

class RemoveStylesCommand : public Command {
 public:
								RemoveStylesCommand(
									StyleContainer* container,
									Style** const styles,
									int32 count);
	virtual						~RemoveStylesCommand();
	
	virtual	status_t			InitCheck();

	virtual	status_t			Perform();
	virtual status_t			Undo();

	virtual void				GetName(BString& name);

 private:
			StyleContainer*		fContainer;
			struct StyleInfo {
				Style*			style;
				int32			index;
				BList			shapes;
			};
			StyleInfo*			fInfos;
			int32				fCount;
			bool				fStylesRemoved;
};

#endif // REMOVE_STYLES_COMMAND_H
