/*
 * Copyright 2003-2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef _LOCALE_ROSTER_H_
#define _LOCALE_ROSTER_H_


#include <String.h>


class BLanguage;
class BLocale;
class BCollator;
class BCountry;
class BCatalog;
class BCatalogAddOn;
class BMessage;

struct entry_ref;

namespace BPrivate {
	class EditableCatalog;
}

enum {
	B_LOCALE_CHANGED	= '_LCC',
};


class BLocaleRoster {
	public:
		BLocaleRoster();
		~BLocaleRoster();

//		status_t GetCatalog(BLocale *,const char *mimeType, BCatalog *catalog);
//		status_t GetCatalog(const char *mimeType, BCatalog *catalog);
//		status_t SetCatalog(BLocale *,const char *mimeType, BCatalog *catalog);

//		status_t GetLocaleFor(const char *langCode, const char *countryCode);

		status_t GetSystemCatalog(BCatalogAddOn **) const;
		status_t GetDefaultCollator(BCollator **) const;
		status_t GetDefaultLanguage(BLanguage **) const;
		status_t GetDefaultCountry(BCountry **) const;
		void SetDefaultCountry(BCountry *) const;

			status_t			GetLanguage(const char* languageCode,
									BLanguage** _language) const;

		status_t GetPreferredLanguages(BMessage *) const;
		status_t SetPreferredLanguages(BMessage *);
			// the message contains one or more 'language'-string-fields
			// which contain the language-name(s)

		status_t GetInstalledLanguages(BMessage *) const;
			// the message contains one or more 'language'-string-fields
			// which contain the language-name(s)

		status_t GetInstalledCatalogs(BMessage *, const char* sigPattern = NULL,
			const char* langPattern = NULL,	int32 fingerprint = 0) const;
			// the message contains...

		static const char *kCatLangAttr;
		static const char *kCatSigAttr;
		static const char *kCatFingerprintAttr;

		static const char *kCatManagerMimeType;
		static const char *kCatEditorMimeType;

		static const char *kEmbeddedCatAttr;
		static int32 kEmbeddedCatResId;

	private:

		BCatalogAddOn* LoadCatalog(const char *signature,
			const char *language = NULL, int32 fingerprint = 0);
		BCatalogAddOn* LoadEmbeddedCatalog(entry_ref *appOrAddOnRef);
		status_t UnloadCatalog(BCatalogAddOn *addOn);

		BCatalogAddOn* CreateCatalog(const char *type,
			const char *signature, const char *language);

		friend class BCatalog;
		friend class BPrivate::EditableCatalog;
		friend status_t get_add_on_catalog(BCatalog*, const char *);
};

#endif	/* _LOCALE_ROSTER_H_ */
