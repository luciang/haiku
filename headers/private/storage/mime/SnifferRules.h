//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file SnifferRules.h
	SnifferRules class declarations
*/

#ifndef _MIME_SNIFFER_RULES_H
#define _MIME_SNIFFER_RULES_H

#include <SupportDefs.h>

#include <list>
#include <string>

struct entry_ref;
class BString;

namespace BPrivate {
namespace Storage {

namespace Sniffer {
	class Rule;
}

namespace Mime {

class SnifferRules {
public:
	SnifferRules();
	~SnifferRules();
	
	status_t GuessMimeType(const entry_ref *ref, BString *type);
	status_t GuessMimeType(const void *buffer, int32 length, BString *type);
	
	status_t SetSnifferRule(const char *type, const char *rule);
	status_t DeleteSnifferRule(const char *type);
	
	void PrintToStream() const;

	struct sniffer_rule {
		std::string type;							// The mime type that own the rule
		std::string rule_string;					// The unparsed string version of the rule
		BPrivate::Storage::Sniffer::Rule *rule;		// The parsed rule
		
		sniffer_rule(BPrivate::Storage::Sniffer::Rule *rule = NULL);
		~sniffer_rule(); 
	};		
private:
	status_t BuildRuleList();
	status_t GuessMimeType(BPositionIO *data, BString *type);
	ssize_t MaxBytesNeeded();
	status_t ProcessType(const char *type, ssize_t *bytesNeeded);

	std::list<sniffer_rule> fRuleList;
	ssize_t fMaxBytesNeeded;
	bool fHaveDoneFullBuild;
};

} // namespace Mime
} // namespace Storage
} // namespace BPrivate

#endif	// _MIME_SNIFFER_H
