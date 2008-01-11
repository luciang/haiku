//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file SupportingApps.h
	SupportingApps class declarations
*/

#ifndef _MIME_SUPPORTING_APPS_H
#define _MIME_SUPPORTING_APPS_H

#include <SupportDefs.h>

#include <map>
#include <set>
#include <string>

class BMessage;

namespace BPrivate {
namespace Storage {
namespace Mime {

class SupportingApps {
public:
	SupportingApps();
	~SupportingApps();
		
	status_t GetSupportingApps(const char *type, BMessage *apps);	

	status_t SetSupportedTypes(const char *app, const BMessage *types, bool fullSync);
	status_t DeleteSupportedTypes(const char *app, bool fullSync);
private:
	status_t AddSupportingApp(const char *type, const char *app);
	status_t RemoveSupportingApp(const char *type, const char *app);

	status_t BuildSupportingAppsTable();

	std::map<std::string, std::set<std::string> > fSupportedTypes;	// app sig => set of supported types
	std::map<std::string, std::set<std::string> > fSupportingApps;	// mime type => set of supporting apps
	std::map<std::string, std::set<std::string> > fStrandedTypes;	// app sig => set of no longer supported types for whom the
																	//            given app is still listed as a supporting app
	bool fHaveDoneFullBuild;
};

} // namespace Mime
} // namespace Storage
} // namespace BPrivate

#endif	// _MIME_SUPPORTING_APPS_H
