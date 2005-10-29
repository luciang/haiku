//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file Database.cpp
	Database class declarations
*/

#ifndef _MIME_DATABASE_H
#define _MIME_DATABASE_H

#include <Mime.h>
#include <mime/AssociatedTypes.h>
#include <mime/InstalledTypes.h>
#include <mime/SnifferRules.h>
#include <mime/SupportingApps.h>
#include <mime/database_access.h>
#include <Messenger.h>
#include <StorageDefs.h>

#include <string>
#include <map>
#include <set>

class BNode;
class BBitmap;
class BMessage;
class BString;

struct entry_ref;

namespace BPrivate {
namespace Storage {
namespace Mime {

// types of mime update functions that may be run asynchronously
typedef enum {
	B_REG_UPDATE_MIME_INFO,
	B_REG_CREATE_APP_META_MIME,
} mime_update_function;

class Database  {
public:
	Database();
	~Database();
	
	status_t InitCheck() const;

	// Type management
	status_t Install(const char *type);
	status_t Delete(const char *type);
	
	// Set()
	status_t SetAppHint(const char *type, const entry_ref *ref);
	status_t SetAttrInfo(const char *type, const BMessage *info);
	status_t SetShortDescription(const char *type, const char *description);	
	status_t SetLongDescription(const char *type, const char *description);
	status_t SetFileExtensions(const char *type, const BMessage *extensions);
	status_t SetIcon(const char *type, const void *data, size_t dataSize, icon_size which);
	status_t SetIconForType(const char *type, const char *fileType, const void *data,
							size_t dataSize, icon_size which);
	status_t SetPreferredApp(const char *type, const char *signature, app_verb verb = B_OPEN);
	status_t SetSnifferRule(const char *type, const char *rule);
	status_t SetSupportedTypes(const char *type, const BMessage *types, bool fullSync);

	// Non-atomic Get()
	status_t GetInstalledSupertypes(BMessage *super_types);
	status_t GetInstalledTypes(BMessage *types);
	status_t GetInstalledTypes(const char *super_type,
									  BMessage *subtypes);
	status_t GetSupportingApps(const char *type, BMessage *signatures);
	status_t GetAssociatedTypes(const char *extension, BMessage *types);

	// Sniffer
	status_t GuessMimeType(const entry_ref *file, BString *result);
	status_t GuessMimeType(const void *buffer, int32 length, BString *result);
	status_t GuessMimeType(const char *filename, BString *result);

	// Monitor
	status_t StartWatching(BMessenger target);
	status_t StopWatching(BMessenger target);

	// Delete()
	status_t DeleteAppHint(const char *type);
	status_t DeleteAttrInfo(const char *type);
	status_t DeleteShortDescription(const char *type);
	status_t DeleteLongDescription(const char *type);
	status_t DeleteFileExtensions(const char *type);
	status_t DeleteIcon(const char *type, icon_size size);
	status_t DeleteIconForType(const char *type, const char *fileType, icon_size which);
	status_t DeletePreferredApp(const char *type, app_verb verb = B_OPEN);
	status_t DeleteSnifferRule(const char *type);
	status_t DeleteSupportedTypes(const char *type, bool fullSync);
	
private:	
	// Functions to send monitor notifications
	status_t SendInstallNotification(const char *type);
	status_t SendDeleteNotification(const char *type);	
	status_t SendMonitorUpdate(int32 which, const char *type, const char *extraType,
	                             bool largeIcon, int32 action);
	status_t SendMonitorUpdate(int32 which, const char *type, const char *extraType,
	                             int32 action);
	status_t SendMonitorUpdate(int32 which, const char *type, bool largeIcon,
	                             int32 action);
	status_t SendMonitorUpdate(int32 which, const char *type,
	                             int32 action);
	status_t SendMonitorUpdate(BMessage &msg);
	
	status_t fStatus;
	std::set<BMessenger> fMonitorMessengers;
	AssociatedTypes fAssociatedTypes;
	InstalledTypes fInstalledTypes;
	SnifferRules fSnifferRules;
	SupportingApps fSupportingApps;
};

} // namespace Mime
} // namespace Storage
} // namespace BPrivate

#endif	// _MIME_DATABASE_H
