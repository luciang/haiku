//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		ClientFontList.cpp
//	Author:			DarkWyrm (bpmagic@columbus.rr.com)
//	Description:	Maintainer object for the list of font families and styles which is
//					kept on the client side.
//------------------------------------------------------------------------------

#include "ClientFontList.h"
#include <File.h>
#include <Message.h>
#include <String.h>
#include <stdio.h>
#include <string.h>
#include <String.h>

#include <PortLink.h>
#include <ServerProtocol.h>
#include <ServerConfig.h>

//#define DEBUG_CLIENT_FONT_LIST
#ifdef DEBUG_CLIENT_FONT_LIST
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

class FontListFamily
{
public:
	FontListFamily(void);
	~FontListFamily(void);
	BString name;
	BList *styles;
	int32 flags;
};

FontListFamily::FontListFamily(void)
{
	styles=new BList(0);
	flags=0;
}

FontListFamily::~FontListFamily(void)
{
	BString *s;
	s=(BString*)styles->RemoveItem(0L);
	while(s)
	{
		delete s;
		s=(BString*)styles->RemoveItem(0L);
	}
	delete styles;
}

ClientFontList::ClientFontList(void)
{
	STRACE(("ClientFontList()\n"));
	familylist=new BList(0);
	fontlock=create_sem(1,"fontlist_sem");
}

ClientFontList::~ClientFontList(void)
{
	STRACE(("~ClientFontList()\n"));
	
	acquire_sem(fontlock);

	font_family *fam;
	while(familylist->ItemAt(0L)!=NULL)
	{
		fam=(font_family *)familylist->RemoveItem(0L);
		delete fam;
	}
	familylist->MakeEmpty();
	delete familylist;
	delete_sem(fontlock);
}

bool ClientFontList::Update(bool check_only)
{
	STRACE(("ClientFontList::Update(%s) - %s\n", (check_only)?"true":"false",SERVER_FONT_LIST));
	
	// Open the font list kept in font list
	acquire_sem(fontlock);

	// We're going to ask the server whether the list has changed
	port_id serverport;
	serverport=find_port(SERVER_PORT_NAME);
	
	bool needs_update=true;
	BPortLink serverlink(serverport);

	if(serverport!=B_NAME_NOT_FOUND)
	{
		int32 code=SERVER_FALSE;
		serverlink.StartMessage(AS_QUERY_FONTS_CHANGED);
		serverlink.Flush();
		serverlink.GetNextReply(&code);
	
		// Attached Data: none
		// Reply: SERVER_TRUE if fonts have changed, SERVER_FALSE if not
		
		needs_update=(code==SERVER_TRUE)?true:false;
	}
	else
	{
		STRACE(("ClientFontList::Update(): Couldn't find app_server port\n"));
	}

	if(check_only)
	{
		release_sem(fontlock);
		return needs_update;
	}

	// Don't update the list if nothing has changed
	if(needs_update)
	{		
		BFile file(SERVER_FONT_LIST,B_READ_ONLY);
		BMessage fontmsg, familymsg;
	
		if(file.InitCheck()==B_OK)
		{
			if(fontmsg.Unflatten(&file)==B_OK)
			{
				#ifdef DEBUG_CLIENT_FONT_LIST
				printf("Font message contents:\n");
				fontmsg.PrintToStream();
				#endif

				// Empty the font list
				FontListFamily *flf=(FontListFamily*)familylist->RemoveItem(0L);
				BString sty, extra;
				int32 famindex, styindex;
				bool tempbool;
				
				while(flf)
				{
					STRACE(("Removing %s from list\n",flf->name.String()));
					delete flf;
					flf=(FontListFamily*)familylist->RemoveItem(0L);
				}
				STRACE(("\n"));
	
				famindex=0;
				
				// Repopulate with new listings
				while(fontmsg.FindMessage("family",famindex,&familymsg)==B_OK)
				{
					famindex++;
					
					flf=new FontListFamily();
					familylist->AddItem(flf);
					familymsg.FindString("name",&(flf->name));

					STRACE(("Adding %s to list\n",flf->name.String()));
					styindex=0;

					// populate family with styles
					while(familymsg.FindString("styles",styindex,&sty)==B_OK)
					{
						STRACE(("\tAdding %s\n",sty.String()));
						styindex++;
						flf->styles->AddItem(new BString(sty));
					}
					
					if(familymsg.FindBool("tuned",&tempbool)==B_OK)
					{
						STRACE(("Family %s has tuned fonts\n", flf->name.String()));
						flf->flags|=B_HAS_TUNED_FONT;
					}
						
					if(familymsg.FindBool("fixed",&tempbool)==B_OK)
					{
						STRACE(("Family %s is fixed-width\n", flf->name.String()));
						flf->flags|=B_IS_FIXED;
					}
					familymsg.MakeEmpty();
					
				}

				serverlink.StartMessage(AS_UPDATED_CLIENT_FONTLIST);
				serverlink.Flush();
					
				release_sem(fontlock);
				return false;

			} // end if Unflatten==B_OK		
		}	// end if InitCheck==B_OK
	} // end if needs_update
	
	release_sem(fontlock);
	return false;
}

int32 ClientFontList::CountFamilies(void)
{
STRACE(("ClientFontList::CountFamilies\n"));
	acquire_sem(fontlock);
	int32 count=familylist->CountItems();
	release_sem(fontlock);
	return count;
}

status_t ClientFontList::GetFamily(int32 index, font_family *name, uint32 *flags)
{
	STRACE(("ClientFontList::GetFamily(%ld)\n",index));
	if(!name)
	{
		STRACE(("ClientFontList::GetFamily: NULL font_family parameter\n"));
		return B_ERROR;
	}
	
	acquire_sem(fontlock);
	FontListFamily *flf=(FontListFamily*)familylist->ItemAt(index);
	if(!flf)
	{
		STRACE(("ClientFontList::GetFamily: index not found\n"));
		return B_ERROR;
	}
	strcpy(*name,flf->name.String());
	
	
	release_sem(fontlock);

	return B_OK;
}

int32 ClientFontList::CountStyles(font_family f)
{
	acquire_sem(fontlock);
	
	FontListFamily *flf=NULL;
	int32 i, count=familylist->CountItems();
	bool found=false;

	for(i=0; i<count;i++)
	{
		flf=(FontListFamily *)familylist->ItemAt(i);
		if(!flf)
			continue;
		if(flf->name.ICompare(f)==0)
		{
			found=true;
			break;
		}
	}
	
	count=(found)?flf->styles->CountItems():0;	
	
	release_sem(fontlock);
	return count;
}

status_t ClientFontList::GetStyle(font_family family, int32 index, font_style *name,uint32 *flags, uint16 *face)
{
	if(!name || !(*name) || !family)
		return B_ERROR;
	
	acquire_sem(fontlock);
	
	FontListFamily *flf=NULL;
	BString *style;
	int32 i, count=familylist->CountItems();
	bool found=false;

	for(i=0; i<count;i++)
	{
		flf=(FontListFamily *)familylist->ItemAt(i);
		if(!flf)
			continue;
		if(flf->name.ICompare(family)==0)
		{
			found=true;
			break;
		}
	}
	
	if(!found)
	{
		release_sem(fontlock);
		return B_ERROR;
	}
	
	style=(BString*)flf->styles->ItemAt(index);
	if(!style)
	{
		release_sem(fontlock);
		return B_ERROR;
	}

	strcpy(*name,style->String());

	if(flags)
		*flags=flf->flags;

	if(face)
	{
		if(style->ICompare("Roman")==0 || 
			style->ICompare("Regular")==0 ||
			style->ICompare("Normal")==0 ||
			style->ICompare("Light")==0 ||
			style->ICompare("Medium")==0 ||
			style->ICompare("Plain")==0) 
		{
			*face|=B_REGULAR_FACE;
			STRACE(("GetStyle: %s Roman face\n", style->String()));
		}
		else
			if(style->ICompare("Bold")==0)
			{
				*face|=B_BOLD_FACE;
				STRACE(("GetStyle: %s Bold face\n"));
			}
			else
				if(style->ICompare("Italic")==0)
				{
					*face|=B_ITALIC_FACE;
					STRACE(("GetStyle: %s Italic face\n"));
				}
				else
					if(style->ICompare("Bold Italic")==0)
					{
						*face|=B_ITALIC_FACE | B_BOLD_FACE;
						STRACE(("GetStyle: %s Bold Italic face\n"));
					}
					else
					{
						STRACE(("GetStyle: %s Unknown face %s\n", style->String()));
					}
	}	
	
	release_sem(fontlock);
	return B_OK;
}

