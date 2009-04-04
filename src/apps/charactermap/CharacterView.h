/*
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHARACTER_VIEW_H
#define CHARACTER_VIEW_H


#include <Messenger.h>
#include <View.h>


class CharacterView : public BView {
public:
							CharacterView(const char* name);
	virtual					~CharacterView();

			void			SetTarget(BMessenger target, uint32 command);

			void			SetCharacterFont(const BFont& font);
			const BFont&	CharacterFont() { return fCharacterFont; }

			void			ShowPrivateBlocks(bool show);
			bool			IsShowingPrivateBlocks() const
								{ return fShowPrivateBlocks; }

			void			ShowContainedBlocksOnly(bool show);
			bool			IsShowingContainedBlocksOnly() const
								{ return fShowContainedBlocksOnly; }

			bool			IsShowingBlock(int32 blockIndex) const;

			void			ScrollTo(int32 blockIndex);

	static	void			UnicodeToUTF8(uint32 c, char* text,
								size_t textSize);
	static	void			UnicodeToUTF8Hex(uint32 c, char* text,
								size_t textSize);

protected:
	virtual void			MessageReceived(BMessage* message);

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();

	virtual	BSize			MinSize();

	virtual void			FrameResized(float width, float height);
	virtual void			MouseDown(BPoint where);
	virtual void			MouseUp(BPoint where);
	virtual void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);

	virtual void			Draw(BRect updateRect);

	virtual void			DoLayout();

private:
			int32			_BlockAt(BPoint point);
			bool 			_GetCharacterAt(BPoint point, uint32& character,
								BRect* _frame = NULL);
			void			_UpdateSize();
			bool			_GetTopmostCharacter(uint32& character,
								int32& offset);
			BRect			_FrameFor(uint32 character);
			void			_CopyToClipboard(const char* text);

private:
			BMessenger		fTarget;
			uint32			fTargetCommand;
			BPoint			fClickPoint;
			bool			fHasCharacter;
			uint32			fCurrentCharacter;
			BRect			fCurrentCharacterFrame;
			bool			fHasTopCharacter;
			uint32			fTopCharacter;
			int32			fTopOffset;

			bool			fShowPrivateBlocks;
			bool			fShowContainedBlocksOnly;

			BRect			fDataRect;
			BFont			fCharacterFont;
			int32			fCharactersPerLine;
			int32			fCharacterWidth;
			int32			fCharacterHeight;
			int32			fCharacterBase;
			int32			fTitleHeight;
			int32			fTitleBase;
			int32			fGap;
			int32			fTitleGap;
			int32*			fTitleTops;
};

#endif	// CHARACTER_VIEW_H
