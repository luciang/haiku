/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 */

/**	Handles the largest part of the font subsystem */


#include <String.h>
#include <Directory.h>
#include <Entry.h>
#include <storage/Path.h>	// specified to be able to build under Dano
#include <File.h>
#include <Message.h>
#include <String.h>

#include <FontServer.h>
#include <FontFamily.h>
#include <ServerFont.h>
#include "ServerConfig.h"


extern FTC_Manager ftmanager; 
FT_Library ftlib;
FontServer *gFontServer = NULL;

//#define PRINT_FONT_LIST

/*!
	\brief Access function to request a face via the FreeType font cache
*/
static FT_Error
face_requester(FTC_FaceID face_id, FT_Library library,
	FT_Pointer request_data, FT_Face *aface)
{ 
	CachedFace face = (CachedFace) face_id;
	return FT_New_Face(ftlib, face->file_path.String(), face->face_index,aface); 
} 


//	#pragma mark -


//! Does basic set up so that directories can be scanned
FontServer::FontServer(void)
	: BLocker("font server lock"),
	fFamilies(20),
	fPlain(NULL),
	fBold(NULL),
	fFixed(NULL)
{
	fInit = FT_Init_FreeType(&ftlib) == 0;

/*
	Fire up the font caching subsystem.
	The three zeros tell FreeType to use the defaults, which are 2 faces,
	4 face sizes, and a maximum of 200000 bytes. I will probably change
	these numbers in the future to maximize performance for your "average"
	application.
*/
	if (FTC_Manager_New(ftlib, 0, 0, 0, &face_requester, NULL, &ftmanager) != 0)
		fInit = false;
}


//! Frees items allocated in the constructor and shuts down FreeType
FontServer::~FontServer(void)
{
	FTC_Manager_Done(ftmanager);
	FT_Done_FreeType(ftlib);
}


/*!
	\brief Counts the number of font families available
	\return The number of unique font families currently available
*/
int32 FontServer::CountFamilies(void)
{
	if (fInit)
		return fFamilies.CountItems();

	return 0;
}


/*!
	\brief Counts the number of styles available in a font family
	\param family Name of the font family to scan
	\return The number of font styles currently available for the font family
*/
int32
FontServer::CountStyles(const char *familyName)
{
	FontFamily *family = GetFamily(familyName);
	if (family)
		return family->CountStyles();

	return 0;
}


/*!
	\brief Removes a font family from the font list
	\param family The family to remove
*/
void
FontServer::RemoveFamily(const char *familyName)
{
	FontFamily *family = GetFamily(familyName);
	if (family) {
		fFamilies.RemoveItem(family);
		delete family;
	}
}


const char*
FontServer::GetFamilyName(uint16 id) const
{
	for (int32 i = 0; i < fFamilies.CountItems(); i++) {
		FontFamily* family = (FontFamily*)fFamilies.ItemAt(i);
		if (family && family->GetID() == id)
			return family->Name();
	}

	return NULL;
}


const char*
FontServer::GetStyleName(const char* familyName, uint16 id) const
{
	FontStyle* style = GetStyle(familyName, id);
	if (style != NULL)
		return style->Name();

	return NULL;
}


FontStyle*
FontServer::GetStyle(const char* familyName, uint16 id) const
{
	FontFamily* family = GetFamily(familyName);

	for (int32 i = 0; i < family->CountStyles(); i++) {
		FontStyle* style = family->GetStyle(i);
		if (style && style->GetID() == id)
			return style;
	}

	return NULL;
}


/*!
	\brief Protected function which locates a FontFamily object
	\param name The family to find
	\return Pointer to the specified family or NULL if not found.
	
	Do NOT delete the FontFamily returned by this function.
*/
FontFamily*
FontServer::GetFamily(const char* name) const
{
	if (!fInit)
		return NULL;

	int32 count = fFamilies.CountItems();

	for (int32 i = 0; i < count; i++) {
		FontFamily *family = (FontFamily*)fFamilies.ItemAt(i);
		if (!strcmp(family->Name(), name))
			return family;
	}

	return NULL;
}

//! Scans the four default system font folders
void
FontServer::ScanSystemFolders(void)
{
	ScanDirectory("/boot/beos/etc/fonts/ttfonts/");
	
	// We don't scan these in test mode to help shave off some startup time
#if !TEST_MODE
	ScanDirectory("/boot/beos/etc/fonts/PS-Type1/");
	ScanDirectory("/boot/home/config/fonts/ttfonts/");
	ScanDirectory("/boot/home/config/fonts/psfonts/");
#endif
}

/*!
	\brief Scan a folder for all valid fonts
	\param fontspath Path of the folder to scan.
	\return 
	- \c B_OK				Success
	- \c B_NAME_TOO_LONG	The path specified is too long
	- \c B_ENTRY_NOT_FOUND	The path does not exist
	- \c B_LINK_LIMIT		A cyclic loop was detected in the file system
	- \c B_BAD_VALUE		Invalid input specified
	- \c B_NO_MEMORY		Insufficient memory to open the folder for reading
	- \c B_BUSY				A busy node could not be accessed
	- \c B_FILE_ERROR		An invalid file prevented the operation.
	- \c B_NO_MORE_FDS		All file descriptors are in use (too many open files). 
*/
status_t
FontServer::ScanDirectory(const char *directoryPath)
{
	// This bad boy does all the real work. It loads each entry in the
	// directory. If a valid font file, it adds both the family and the style.
	// Both family and style are stored internally as BStrings. Once everything

	BDirectory dir;
	status_t status = dir.SetTo(directoryPath);
	if (status != B_OK)
		return status;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		status = entry.GetPath(&path);
		if (status < B_OK)
			continue;

		FT_Face face;
		FT_Error error = FT_New_Face(ftlib, path.Path(), 0, &face);
		if (error != 0)
			continue;

// TODO: Commenting this out makes my "Unicode glyph lookup"
// work with our default fonts. The real fix is to select the
// Unicode char map (if supported), and/or adjust the
// utf8 -> glyph-index mapping everywhere to handle other
// char maps. We could also ignore fonts that don't support
// the Unicode lookup as a temporary "solution".
#if 0
		FT_CharMap charmap = _GetSupportedCharmap(face);
		if (!charmap) {
		    FT_Done_Face(face);
		    continue;
    	}

		face->charmap = charmap;
#endif

	    FontFamily *family = GetFamily(face->family_name);
		if (family == NULL) {
			#ifdef PRINT_FONT_LIST
			printf("Font Family: %s\n", face->family_name);
			#endif

			family = new FontFamily(face->family_name, fFamilies.CountItems());
			fFamilies.AddItem(family);
		}

		if (family->HasStyle(face->style_name)) {
			FT_Done_Face(face);
			continue;
		}

		#ifdef PRINT_FONT_LIST
		printf("\tFont Style: %s\n", face->style_name);
		#endif

	    FontStyle *style = new FontStyle(path.Path(), face);
		if (!family->AddStyle(style))
			delete style;

		// FT_Face is kept open in FontStyle and will be unset in the
		// FontStyle destructor
	}

	fNeedUpdate = true;
	return B_OK;
}


/*!
	\brief Finds and returns the first valid charmap in a font
	
	\param face Font handle obtained from FT_Load_Face()
	\return An FT_CharMap or NULL if unsuccessful
*/
FT_CharMap
FontServer::_GetSupportedCharmap(const FT_Face& face)
{
	for (int32 i = 0; i < face->num_charmaps; i++) {
		FT_CharMap charmap = face->charmaps[i];

		switch (charmap->platform_id) {
			case 3:
				// if Windows Symbol or Windows Unicode
				if (charmap->encoding_id == 0 || charmap->encoding_id == 1)
					return charmap;
				break;

			case 1:
				// if Apple Unicode
				if (charmap->encoding_id == 0)
					return charmap;
				break;

			case 0:
				// if Apple Roman
				if (charmap->encoding_id == 0)
					return charmap;
				break;

			default:
				break;
		}
	}

	return NULL;
}


/*!
	\brief This saves all family names and styles to the file specified in
	ServerConfig.h as SERVER_FONT_LIST as a flattened BMessage.

	This operation is not done very often because the access to disk adds a significant 
	performance hit.

	The format for storage consists of two things: an array of strings with the name 'family'
	and a number of small string arrays which have the name of the font family. These are
	the style lists. 

	Additionally, any fonts which have bitmap strikes contained in them or any fonts which
	are fixed-width are named in the arrays 'tuned' and 'fixed'.
*/
void
FontServer::SaveList(void)
{
/*	int32 famcount=0, stycount=0,i=0,j=0;
	FontFamily *fam;
	FontStyle *sty;
	BMessage fontmsg, familymsg('FONT');
	BString famname, styname, extraname;
	bool fixed,tuned;
	
	famcount=families->CountItems();
	for(i=0; i<famcount; i++)
	{
		fam=(FontFamily*)families->ItemAt(i);
		fixed=false;
		tuned=false;
		if(!fam)
			continue;

		famname=fam->Name();
				
		// Add the family to the message
		familymsg.AddString("name",famname);
		
		stycount=fam->CountStyles();
		for(j=0;j<stycount;j++)
		{
			styname.SetTo(fam->GetStyle(j));
			if(styname.CountChars()>0)
			{
				// Add to list
				familymsg.AddString("styles", styname);
				
				// Check to see if it has prerendered strikes (has "tuned" fonts)
				sty=fam->GetStyle(styname.String());
				if(!sty)
					continue;
				
				if(sty->HasTuned() && sty->IsScalable())
					tuned=true;

				// Check to see if it is fixed-width
				if(sty->IsFixedWidth())
					fixed=true;
			}
		}
		if(tuned)
			familymsg.AddBool("tuned",true);
		if(fixed)
			familymsg.AddBool("fixed",true);
		
		fontmsg.AddMessage("family",&familymsg);
		familymsg.MakeEmpty();
	}
	
	BFile file(SERVER_FONT_LIST,B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if(file.InitCheck()==B_OK)
		fontmsg.Flatten(&file);
*/
}


/*!
	\brief Retrieves the FontStyle object
	\param family The font's family
	\param style The font's style
	\return The FontStyle having those attributes or NULL if not available
*/
FontStyle*
FontServer::GetStyle(const char* familyName, const char* styleName)
{
	FontFamily* family = GetFamily(familyName);

	if (family)
		return family->GetStyle(styleName);

	return NULL;
}


/*!
	\brief Retrieves the FontStyle object
	\param family ID for the font's family
	\param style ID of the font's style
	\return The FontStyle having those attributes or NULL if not available
*/
FontStyle*
FontServer::GetStyle(const uint16& familyID, const uint16& styleID)
{
	FontFamily *family = GetFamily(familyID);

	if (family)
		return family->GetStyle(styleID);

	return NULL;
}


FontFamily*
FontServer::GetFamily(const uint16& familyID) const
{
	for (int32 i = 0; i < fFamilies.CountItems(); i++) {
		FontFamily *family = (FontFamily*)fFamilies.ItemAt(i);
		if (family->GetID() == familyID)
			return family;
	}

	return NULL;
}


/*!
	\brief Returns the current object used for the regular style
	\return A ServerFont pointer which is the plain font.
	
	Do NOT delete this object. If you access it, make a copy of it.
*/
ServerFont*
FontServer::GetSystemPlain()
{
	return fPlain;
}


/*!
	\brief Returns the current object used for the bold style
	\return A ServerFont pointer which is the bold font.
	
	Do NOT delete this object. If you access it, make a copy of it.
*/
ServerFont*
FontServer::GetSystemBold()
{
	return fBold;
}


/*!
	\brief Returns the current object used for the fixed style
	\return A ServerFont pointer which is the fixed font.
	
	Do NOT delete this object. If you access it, make a copy of it.
*/
ServerFont*
FontServer::GetSystemFixed()
{
	return fFixed;
}


/*!
	\brief Sets the system's plain font to the specified family and style
	\param family Name of the font's family
	\param style Name of the style desired
	\param size Size desired
	\return true if successful, false if not.
	
*/
bool
FontServer::SetSystemPlain(const char* familyName, const char* styleName, float size)
{
	FontStyle *style = GetStyle(familyName, styleName);
	if (style == NULL)
		return false;

	delete fPlain;
	fPlain = new ServerFont(style, size);

	return true;
}


/*!
	\brief Sets the system's bold font to the specified family and style
	\param family Name of the font's family
	\param style Name of the style desired
	\param size Size desired
	\return true if successful, false if not.
	
*/
bool
FontServer::SetSystemBold(const char* familyName, const char* styleName, float size)
{
	FontStyle *style = GetStyle(familyName, styleName);
	if (style == NULL)
		return false;

	delete fBold;
	fBold = new ServerFont(style, size);

	return true;
}


/*!
	\brief Sets the system's fixed font to the specified family and style
	\param family Name of the font's family
	\param style Name of the style desired
	\param size Size desired
	\return true if successful, false if not.
	
*/
bool
FontServer::SetSystemFixed(const char* familyName, const char* styleName, float size)
{
	FontStyle *style = GetStyle(familyName, styleName);
	if (style == NULL)
		return false;

	delete fFixed;
	fFixed = new ServerFont(style, size);

	return true;
}

