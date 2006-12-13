/* BMailFileConfigView - a file configuration view for filters
**
** Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
*/

#include <BeBuild.h>

class _EXPORT BFileControl;
class _EXPORT BMailFileConfigView;

#include <TextControl.h>
#include <Button.h>
#include <String.h>
#include <Message.h>
#include <Path.h>

#include <stdio.h>

#include <FileConfigView.h>

#include <MDRLanguage.h>

const uint32 kMsgSelectButton = 'fsel';

BFileControl::BFileControl(BRect rect,const char *name,const char *label,const char *pathOfFile,uint32 flavors)
		:	BView(rect,name,B_FOLLOW_LEFT | B_FOLLOW_TOP,0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	// determine font height
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float itemHeight = (int32)(fontHeight.ascent + fontHeight.descent + fontHeight.leading) + 13;
	float labelWidth = StringWidth("Select" B_UTF8_ELLIPSIS) + 20;
	rect = Bounds();
	rect.right -= labelWidth;
	rect.top = 4;	rect.bottom = itemHeight + 2;
	fText = new BTextControl(rect,"file_path",label,pathOfFile,NULL);
	if (label)
		fText->SetDivider(fText->StringWidth(label) + 6);
	AddChild(fText);

	fButton = new BButton(BRect(0,0,1,1),"select_file",MDR_DIALECT_CHOICE ("Select","選択") B_UTF8_ELLIPSIS,
						  new BMessage(kMsgSelectButton));
	fButton->ResizeToPreferred();
	fButton->MoveBy(rect.right + 6, (rect.Height() - fButton->Frame().Height()) / 2);
	AddChild(fButton);

	fPanel = new BFilePanel(B_OPEN_PANEL,NULL,NULL,flavors,false);

	ResizeToPreferred();
}


BFileControl::~BFileControl()
{
	delete fPanel;
}


void BFileControl::AttachedToWindow()
{
	fButton->SetTarget(this);

	BMessenger messenger(this);
	if (messenger.IsValid())
		fPanel->SetTarget(messenger);
}


void BFileControl::MessageReceived(BMessage *msg)
{
	switch (msg->what)
	{
		case kMsgSelectButton:
		{
			fPanel->Hide();
			//fPanel->Window()->SetTitle(title);

			BPath path(fText->Text());
			if (path.InitCheck() >= B_OK)
				if (path.GetParent(&path) >= B_OK)
					fPanel->SetPanelDirectory(path.Path());

			fPanel->Show();
			break;
		}
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (msg->FindRef("refs",&ref) >= B_OK)
			{
				BEntry entry(&ref);
				if (entry.InitCheck() >= B_OK)
				{
					BPath path;
					entry.GetPath(&path);

					fText->SetText(path.Path());
				}
			}
			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}


void BFileControl::SetText(const char *pathOfFile)
{
	fText->SetText(pathOfFile);
}


const char *BFileControl::Text() const
{
	return fText->Text();
}


void BFileControl::SetEnabled(bool enabled)
{
	fText->SetEnabled(enabled);
	fButton->SetEnabled(enabled);
}


void BFileControl::GetPreferredSize(float *width, float *height)
{
	*width = fButton->Frame().right + 5;
	*height = fText->Bounds().Height() + 8;
}


//--------------------------------------------------------------------------
//	#pragma mark -

BMailFileConfigView::BMailFileConfigView(const char *label,const char *name,bool useMeta,const char *defaultPath,uint32 flavors)
		:	BFileControl(BRect(5,0,255,10),name,label,defaultPath,flavors),
		fUseMeta(useMeta),
		fName(name)
{
}


void BMailFileConfigView::SetTo(BMessage *archive, BMessage *meta)
{
	fMeta = meta;
	BString path = (fUseMeta ? meta : archive)->FindString(fName);

	if (path != "")
		SetText(path.String());
}


status_t BMailFileConfigView::Archive(BMessage *into, bool /*deep*/) const
{
	const char *path = Text();
	BMessage *archive = fUseMeta ? fMeta : into;

	if (archive->ReplaceString(fName,path) != B_OK)
		archive->AddString(fName,path);

	return B_OK;
}

