//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file Database.cpp
	Database class implementation
*/

#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <mime/database_access.h>
#include <mime/database_support.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <storage_support.h>
#include <TypeConstants.h>

#include <fs_attr.h>	// For struct attr_info
#include <iostream>
#include <new>			// For new(nothrow)
#include <stdio.h>
#include <string>

#include "mime/Database.h"

//#define DBG(x) x
#define DBG(x)
#define OUT printf

// icon types
enum {
	B_MINI_ICON_TYPE	= 'MICN',
	B_LARGE_ICON_TYPE	= 'ICON',
};

namespace BPrivate {
namespace Storage {
namespace Mime {

/*!
	\class Database
	\brief Mime::Database is the master of the MIME data base.

	All write and non-atomic read accesses are carried out by this class.
	Monitoring is handled by 
	
	\note No error checking (other than checks for NULL pointers) is performed
	      by this class on the mime type strings passed to it. It's assumed
	      that this sort of checking has been done beforehand.
*/

// constructor
/*!	\brief Creates and initializes a MimeDatabase.
*/
Database::Database()
	: fCStatus(B_NO_INIT)
{
	// Do some really minor error checking
	BEntry entry(kDatabaseDir.c_str());
	fCStatus = entry.Exists() ? B_OK : B_BAD_VALUE;
}

// destructor
/*!	\brief Frees all resources associated with this object.
*/
Database::~Database()
{
}

// InitCheck
/*! \brief Returns the initialization status of the object.
	\return
	- B_OK: success
	- "error code": failure
*/
status_t
Database::InitCheck() const
{
	return fCStatus;
}

// Install
/*!	\brief Installs the given type in the database
	\note The R5 version of this call returned an unreliable result if the
	      MIME type was already installed. Ours simply returns B_OK.
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\param decsription Pointer to a NULL-terminated string containing the new long description
	\return
	- B_OK: success
	- B_FILE_EXISTS: the type is already installed
	- "error code": failure
*/
status_t
Database::Install(const char *type)
{
	BEntry entry;
	status_t err = (type ? B_OK : B_BAD_VALUE);
	if (!err) 
		err = entry.SetTo(type_to_filename(type).c_str());
	if (!err) {
		if (entry.Exists())
			err = B_FILE_EXISTS;
		else {
			bool didCreate = false;
			BNode node;
			err = open_or_create_type(type, &node, &didCreate);
			if (didCreate)
				fInstalledTypes.AddType(type);
		}
	}
	return err;
}

// Delete
/*!	\brief Removes the given type from the database
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\return
	- B_OK: success
	- "error code": failure
*/
status_t
Database::Delete(const char *type)
{
	BEntry entry;
	status_t err = (type ? B_OK : B_BAD_VALUE);
	if (!err) 
		err = entry.SetTo(type_to_filename(type).c_str());		 
	if (!err)
		err = entry.Remove();
	if (!err) {
		fInstalledTypes.RemoveType(type);
		err = SendDeleteNotification(type);
	}
	return err;
}

// SetAppHint
/*!	\brief Sets the application hint for the given MIME type
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\param decsription Pointer to an entry_ref containing the location of an application
	       that should be used when launching an application with this signature.
*/
status_t
Database::SetAppHint(const char *type, const entry_ref *ref)
{
	DBG(OUT("Database::SetAppHint()\n"));
	BPath path;
	bool didCreate = false;
	status_t err = (type && ref) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = path.SetTo(ref);
	if (!err)	
		err = write_mime_attr(type, kAppHintAttr, path.Path(), strlen(path.Path())+1,
							    kAppHintType, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_APP_HINT_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

// SetAttrInfo
/*! \brief Stores a BMessage describing the format of attributes typically associated with
	files of the given MIME type
	
	See BMimeType::SetAttrInfo() for description of the expected message format.

	The \c BMessage::what value is ignored.
	
	\param info Pointer to a pre-allocated and properly formatted BMessage containing 
	            information about the file attributes typically associated with the
	            MIME type.
	\return
	- \c B_OK: Success
	- "error code": Failure
*/
status_t
Database::SetAttrInfo(const char *type, const BMessage *info)
{
	DBG(OUT("Database::SetAttrInfo()\n"));	
	bool didCreate = false;
	status_t err = (type && info) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = write_mime_attr_message(type, kAttrInfoAttr, info, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_ATTR_INFO_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

// SetShortDescription
/*!	\brief Sets the short description for the given MIME type
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\param decsription Pointer to a NULL-terminated string containing the new short description
*/
status_t
Database::SetShortDescription(const char *type, const char *description)
{
	DBG(OUT("Database::SetShortDescription()\n"));	
	bool didCreate = false;
	status_t err = (type && description && strlen(description) < B_MIME_TYPE_LENGTH) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = write_mime_attr(type, kShortDescriptionAttr, description, strlen(description)+1,
							    kShortDescriptionType, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_SHORT_DESCRIPTION_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

// SetLongDescription
/*!	\brief Sets the long description for the given MIME type
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\param decsription Pointer to a NULL-terminated string containing the new long description
*/
status_t
Database::SetLongDescription(const char *type, const char *description)
{
	DBG(OUT("Database::SetLongDescription()\n"));
	bool didCreate = false;
	status_t err = (type && description && strlen(description) < B_MIME_TYPE_LENGTH) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = write_mime_attr(type, kLongDescriptionAttr, description, strlen(description)+1,
							    kLongDescriptionType, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_SHORT_DESCRIPTION_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

// SetFileExtensions
//! Sets the list of filename extensions associated with the MIME type
/*! The list of extensions is given in a pre-allocated BMessage pointed to by
	the \c extensions parameter. Please see BMimeType::SetFileExtensions()
	for a description of the expected message format.
	  
	\param extensions Pointer to a pre-allocated, properly formatted BMessage containing
	                  the new list of file extensions to associate with this MIME type.
	\return
	- \c B_OK: Success
	- "error code": Failure
*/
status_t
Database::SetFileExtensions(const char *type, const BMessage *extensions)
{
	DBG(OUT("Database::SetFileExtensions()\n"));	
	bool didCreate = false;
	status_t err = (type && extensions) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = write_mime_attr_message(type, kFileExtensionsAttr, extensions, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_FILE_EXTENSIONS_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

//! Sets the icon for the given mime type
/*! This is the version I would have used if I could have gotten a BBitmap
	to the registrar somehow. Since R5::BBitmap::Instantiate is causing a
	violent crash, I've copied most of the icon	color conversion code into
	Mime::get_icon_data() so BMimeType::SetIcon() can get at it.
	
	Once we have a sufficiently complete OBOS::BBitmap implementation, we
	ought to be able to use this version of SetIcon() again. At that point,
	I'll add some real documentation.
*/
status_t
Database::SetIcon(const char *type, const void *data, size_t dataSize, icon_size which)
{
	return SetIconForType(type, NULL, data, dataSize, which);
}

// SetIconForType
/*! \brief Sets the large or mini icon used by an application of this type for
	files of the given type.

	The type of the \c BMimeType object is not required to actually be a subtype of
	\c "application/"; that is the intended use however, and application-specific
	icons are not expected to be present for non-application types.
		
	The bitmap data pointed to by \c data must be of the proper size (\c 32x32
	for \c B_LARGE_ICON, \c 16x16 for \c B_MINI_ICON) and the proper color
	space (B_CMAP8).
	
	\param type The MIME type
	\param fileType The MIME type whose custom icon you wish to set.
	\param data Pointer to an array of bitmap data of proper dimensions and color depth
	\param dataSize The length of the array pointed to by \c data
	\param size The size icon you're expecting (\c B_LARGE_ICON or \c B_MINI_ICON)
	\return
	- \c B_OK: Success
	- "error code": Failure	

*/
status_t
Database::SetIconForType(const char *type, const char *fileType, const void *data,
							   size_t dataSize, icon_size which)
{
	ssize_t err = (type && data) ? B_OK : B_BAD_VALUE;

	std::string attr;
	int32 attrType = 0;
	size_t attrSize = 0;
	
	// Figure out what kind of data we *should* have
	if (!err) {
		switch (which) {
			case B_MINI_ICON:
				attrType = kMiniIconType;
				attrSize = 16 * 16;
				break;
			case B_LARGE_ICON:
				attrType = kLargeIconType;
				attrSize = 32 * 32;
				break;
			default:
				err = B_BAD_VALUE;
				break;
		}
	}
	
	// Construct our attribute name
	if (fileType) {
		attr = (which == B_MINI_ICON
	              ? kMiniIconAttrPrefix
	                : kLargeIconAttrPrefix)
	                  + BPrivate::Storage::to_lower(fileType);
	} else 	
		attr = which == B_MINI_ICON ? kMiniIconAttr : kLargeIconAttr;
	
	// Double check the data we've been given
	if (!err)
		err = dataSize == attrSize ? B_OK : B_BAD_VALUE;

	// Write the icon data
	BNode node;
	bool didCreate = false;
	if (!err)
		err = open_or_create_type(type, &node, &didCreate);
	if (!err)
		err = node.WriteAttr(attr.c_str(), attrType, 0, data, attrSize);
	if (err >= 0)
		err = err == (ssize_t)attrSize ? B_OK : B_FILE_ERROR;
	return err;			

}

// SetPreferredApp
/*!	\brief Sets the signature of the preferred application for the given app verb
	
	Currently, the only supported app verb is \c B_OPEN	
	\param type Pointer to a NULL-terminated string containing the MIME type of interest
	\param signature Pointer to a NULL-terminated string containing the MIME signature
	                 of the new preferred application
	\param verb \c app_verb action for which the new preferred application is applicable
*/
status_t
Database::SetPreferredApp(const char *type, const char *signature, app_verb verb = B_OPEN)
{
	DBG(OUT("Database::SetPreferredApp()\n"));	
	bool didCreate = false;
	status_t err = (type && signature && strlen(signature) < B_MIME_TYPE_LENGTH) ? B_OK : B_BAD_VALUE;
	if (!err)
		err = write_mime_attr(type, kPreferredAppAttr, signature, strlen(signature)+1,
		                        kPreferredAppType, &didCreate);
	if (!err)
		err = SendMonitorUpdate(B_PREFERRED_APP_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

// SetSupportedTypes
status_t
Database::SetSupportedTypes(const char *type, const BMessage *types, bool fullSync)
{
	DBG(OUT("Database::SetSupportedTypes()\n"));	
	bool didCreate = false;
	status_t err = (type && types) ? B_OK : B_BAD_VALUE;
	// Install the types
	if (!err) {
		const char *supportedType;
		for (int32 i = 0;
			 err == B_OK
			 && types->FindString("types", i, &supportedType) == B_OK;
			 i++) {
			if (!is_installed(supportedType))
				err = Install(supportedType);
		}
	}
	// Write the attr
	if (!err)
		err = write_mime_attr_message(type, kSupportedTypesAttr, types, &didCreate);
	// Update the supporting apps map
	if (!err)
		err = fSupportingApps.SetSupportedTypes(type, types, fullSync);
	// Notify the monitor
	if (!err)
		err = SendMonitorUpdate(B_SUPPORTED_TYPES_CHANGED, type, B_META_MIME_MODIFIED);
	return err;
}

status_t
Database::GetInstalledSupertypes(BMessage *supertypes)
{
	return fInstalledTypes.GetInstalledSupertypes(supertypes);
}

status_t
Database::GetInstalledTypes(BMessage *types)
{
	return fInstalledTypes.GetInstalledTypes(types);
}

status_t
Database::GetInstalledTypes(const char *supertype, BMessage *subtypes)
{
	return fInstalledTypes.GetInstalledTypes(supertype, subtypes);
}

status_t
Database::GetSupportingApps(const char *type, BMessage *signatures)
{
	return fSupportingApps.GetSupportingApps(type, signatures);
}


// StartWatching
//!	Subscribes the given BMessenger to the MIME monitor service
/*!	Notification messages will be sent with a \c BMessage::what value
	of \c B_META_MIME_CHANGED. Notification messages have the following
	fields:
	
	<table>
		<tr>
			<td> Name </td>
			<td> Type </td>
			<td> Description </td>
		</tr>
		<tr>
			<td> \c be:type </td>
			<td> \c B_STRING_TYPE </td>
			<td> The MIME type that was changed </td>
		</tr>
		<tr>
			<td> \c be:which </td>
			<td> \c B_INT32_TYPE </td>
			<td> Bitmask describing which attributes were changed (see below) </td>
		</tr>
		<tr>
			<td> \c be:extra_type </td>
			<td> \c B_STRING_TYPE </td>
			<td> Additional MIME type string (applicable to B_ICON_FOR_TYPE_CHANGED notifications only)</td>
		</tr>
		<tr>
			<td> \c be:large_icon </td>
			<td> \c B_BOOL_TYPE </td>
			<td> \c true if the large icon was changed, \c false if the small icon
			     was changed (applicable to B_ICON_[FOR_TYPE_]CHANGED updates only) </td>
		</tr>		
	</table>
	
	The \c be:which field of the message describes which attributes were updated, and
	may be the bitwise \c OR of any of the following values:
	
	<table>
		<tr>
			<td> Value </td>
			<td> Triggered By </td>
		</tr>
		<tr>
			<td> \c B_ICON_CHANGED </td>
			<td> \c BMimeType::SetIcon() </td>
		</tr>
		<tr>
			<td> \c B_PREFERRED_APP_CHANGED </td>
			<td> \c BMimeType::SetPreferredApp() </td>
		</tr>
		<tr>
			<td> \c B_ATTR_INFO_CHANGED </td>
			<td> \c BMimeType::SetAttrInfo() </td>
		</tr>
		<tr>
			<td> \c B_FILE_EXTENSIONS_CHANGED </td>
			<td> \c BMimeType::SetFileExtensions() </td>
		</tr>
		<tr>
			<td> \c B_SHORT_DESCRIPTION_CHANGED </td>
			<td> \c BMimeType::SetShortDescription() </td>
		</tr>
		<tr>
			<td> \c B_LONG_DESCRIPTION_CHANGED </td>
			<td> \c BMimeType::SetLongDescription() </td>
		</tr>
		<tr>
			<td> \c B_ICON_FOR_TYPE_CHANGED </td>
			<td> \c BMimeType::SetIconForType() </td>
		</tr>
		<tr>
			<td> \c B_APP_HINT_CHANGED </td>
			<td> \c BMimeType::SetAppHint() </td>
		</tr>
	</table>

	\param target The \c BMessenger to subscribe to the MIME monitor service
*/
status_t
Database::StartWatching(BMessenger target)
{
	DBG(OUT("Database::StartWatching()\n"));
	status_t err = target.IsValid() ? B_OK : B_BAD_VALUE;	
	if (!err) 
		fMonitorMessengers.insert(target);
	return err;	
}

// StartWatching
//!	Unsubscribes the given BMessenger from the MIME monitor service
/*! \param target The \c BMessenger to unsubscribe 
*/
status_t
Database::StopWatching(BMessenger target)
{
	DBG(OUT("Database::StopWatching()\n"));
	status_t err = fMonitorMessengers.find(target) != fMonitorMessengers.end() ? B_OK : B_ENTRY_NOT_FOUND;
	if (!err)
		fMonitorMessengers.erase(target);
	return err;	
}

status_t
Database::DeleteAppHint(const char *type)
{
	status_t err = delete_attribute(type, kAppHintAttr);
	if (!err)
		err = SendMonitorUpdate(B_APP_HINT_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteAttrInfo(const char *type)
{
	status_t err = delete_attribute(type, kAttrInfoAttr);
	if (!err)
		err = SendMonitorUpdate(B_ATTR_INFO_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteShortDescription(const char *type)
{
	status_t err = delete_attribute(type, kShortDescriptionAttr);
	if (!err)
		err = SendMonitorUpdate(B_SHORT_DESCRIPTION_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteLongDescription(const char *type)
{
	status_t err = delete_attribute(type, kLongDescriptionAttr);
	if (!err)
		err = SendMonitorUpdate(B_LONG_DESCRIPTION_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteFileExtensions(const char *type)
{
	status_t err = delete_attribute(type, kFileExtensionsAttr);
	if (!err)
		err = SendMonitorUpdate(B_FILE_EXTENSIONS_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteIcon(const char *type, icon_size which)
{
	const char *attr = which == B_MINI_ICON ? kMiniIconAttr : kLargeIconAttr;
	status_t err = delete_attribute(type, attr);
	if (!err)
		err = SendMonitorUpdate(B_ICON_CHANGED, type, which, B_META_MIME_DELETED);
	return err;
}
	
status_t
Database::DeleteIconForType(const char *type, const char *fileType, icon_size which)
{
	std::string attr;
	status_t err = fileType ? B_OK : B_BAD_VALUE;
	if (!err) {
		attr = (which == B_MINI_ICON ? kMiniIconAttrPrefix : kLargeIconAttrPrefix) + BPrivate::Storage::to_lower(fileType);
		err = delete_attribute(type, attr.c_str());
	}
	if (!err)
		err = SendMonitorUpdate(B_ICON_FOR_TYPE_CHANGED, type, fileType,
		                          which == B_LARGE_ICON, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeletePreferredApp(const char *type, app_verb verb = B_OPEN)
{
	status_t err;
	switch (verb) {
		case B_OPEN:
			err = delete_attribute(type, kPreferredAppAttr);
			break;
				
		default:
			err = B_BAD_VALUE;
			break;
	}
	/*! \todo The R5 monitor makes no note of which app_verb value was updated. If
		additional app_verb values besides \c B_OPEN are someday added, the format
		of the MIME monitor messages will need to be augmented.
	*/
	if (!err)
		err = SendMonitorUpdate(B_PREFERRED_APP_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteSnifferRule(const char *type)
{
	status_t err = delete_attribute(type, kSnifferRuleAttr);
	if (!err)
		err = SendMonitorUpdate(B_SNIFFER_RULE_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::DeleteSupportedTypes(const char *type)
{
	status_t err = delete_attribute(type, kSupportedTypesAttr);
	if (!err)
		err = SendMonitorUpdate(B_SUPPORTED_TYPES_CHANGED, type, B_META_MIME_DELETED);
	return err;
}

status_t
Database::SendInstallNotification(const char *type)
{
//	fInstalledTypes.AddType(type);
	status_t err = SendMonitorUpdate(B_MIME_TYPE_CREATED, type, B_META_MIME_MODIFIED);
	return err;
}

status_t
Database::SendDeleteNotification(const char *type)
{
	// Tell the backend first
//	fInstalledTypes.RemoveType(type);
	status_t err = SendMonitorUpdate(B_MIME_TYPE_DELETED, type, B_META_MIME_MODIFIED);
	return err;
}

// SendMonitorUpdate
/*! \brief Sends an update notification to all BMessengers that have
	subscribed to the MIME Monitor service
	\param type The MIME type that was updated
	\param which Bitmask describing which attribute was updated
	\param extraType The MIME type to which the change is applies
	\param largeIcon \true if the the large icon was updated, \false if the
		   small icon was updated
*/
status_t
Database::SendMonitorUpdate(int32 which, const char *type, const char *extraType, bool largeIcon, int32 action) {
	BMessage msg(B_META_MIME_CHANGED);
	status_t err;
		
	err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddString("be:extra_type", extraType);
	if (!err)
		err = msg.AddBool("be:large_icon", largeIcon);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = SendMonitorUpdate(msg);
	return err;
}

// SendMonitorUpdate
/*! \brief Sends an update notification to all BMessengers that have
	subscribed to the MIME Monitor service
	\param type The MIME type that was updated
	\param which Bitmask describing which attribute was updated
	\param extraType The MIME type to which the change is applies
*/
status_t
Database::SendMonitorUpdate(int32 which, const char *type, const char *extraType, int32 action) {
	BMessage msg(B_META_MIME_CHANGED);
	status_t err;
		
	err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddString("be:extra_type", extraType);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = SendMonitorUpdate(msg);
	return err;
}

// SendMonitorUpdate
/*! \brief Sends an update notification to all BMessengers that have
	subscribed to the MIME Monitor service
	\param type The MIME type that was updated
	\param which Bitmask describing which attribute was updated
	\param largeIcon \true if the the large icon was updated, \false if the
		   small icon was updated
*/
status_t
Database::SendMonitorUpdate(int32 which, const char *type, bool largeIcon, int32 action) {
	BMessage msg(B_META_MIME_CHANGED);
	status_t err;
		
	err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddBool("be:large_icon", largeIcon);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = SendMonitorUpdate(msg);
	return err;
}

// SendMonitorUpdate
/*! \brief Sends an update notification to all BMessengers that have
	subscribed to the MIME Monitor service
	\param type The MIME type that was updated
	\param which Bitmask describing which attribute was updated
*/
status_t
Database::SendMonitorUpdate(int32 which, const char *type, int32 action) {
	BMessage msg(B_META_MIME_CHANGED);
	status_t err;
		
	err = msg.AddInt32("be:which", which);
	if (!err)
		err = msg.AddString("be:type", type);
	if (!err)
		err = msg.AddInt32("be:action", action);
	if (!err)
		err = SendMonitorUpdate(msg);
	return err;
}

// SendMonitorUpdate
/*! \brief Sends an update notification to all BMessengers that have subscribed to
	the MIME Monitor service
	\param BMessage A preformatted MIME monitor message to be sent to all subscribers
*/
status_t
Database::SendMonitorUpdate(BMessage &msg) {
	DBG(OUT("Database::SendMonitorUpdate(BMessage&)\n"));
	status_t err;
	std::set<BMessenger>::const_iterator i;
	for (i = fMonitorMessengers.begin(); i != fMonitorMessengers.end(); i++) {
		status_t err = (*i).SendMessage(&msg, (BHandler*)NULL);
		if (err)
			DBG(OUT("Database::SendMonitorUpdate(BMessage&): BMessenger::SendMessage failed, %p\n", err));
	}
	DBG(OUT("Database::SendMonitorUpdate(BMessage&) done\n"));
	err = B_OK;
	return err;
}

} // namespace Mime
} // namespace Storage
} // namespace BPrivate

