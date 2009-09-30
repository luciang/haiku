/*
 * Copyright 2009, Adrien Destugues, pulkomandy@gmail.com. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <HashMapCatalog.h>

#include <ByteOrder.h>

#include <stdlib.h>


/*
 * This is the standard implementation of a localization catalog, using a hash
 * map. This class is abstract, you need to inherit it and provide methodes for
 * reading and writing the catalog to a file. Classes doing that are
 * HashMapCatalog and PlainTextCatalog.
 * If you ever need to create a catalog not built around an hash map, inherit
 * BCatalogAddOn instead. Note that in this case you will not be able to use our
 * development tools anymore.
 */


extern "C" uint32 adler32(uint32 adler, const uint8 *buf, uint32 len);
	// definition lives in adler32.c


CatKey::CatKey(const char *str, const char *ctx, const char *cmt)
	:
	fString(str),
	fContext(ctx),
	fComment(cmt),
	fFlags(0)
{
	fHashVal = HashFun(fString.String(),0);
	fHashVal = HashFun(fContext.String(),fHashVal);
	fHashVal = HashFun(fComment.String(),fHashVal);
}


CatKey::CatKey(uint32 id)
	:
	fHashVal(id),
	fFlags(0)
{
}


CatKey::CatKey()
	:
	fHashVal(0),
	fFlags(0)
{
}


bool
CatKey::operator== (const CatKey& right) const
{
	// Two keys are equal if their hashval and key (string,context,comment)
	// are equal (testing only the hash would not filter out collisions):
	return fHashVal == right.fHashVal
		&& fString == right.fString
		&& fContext == right.fContext
		&& fComment == right.fComment;
}


bool
CatKey::operator!= (const CatKey& right) const
{
	// Two keys are equal if their hashval and key (string,context,comment)
	// are equal (testing only the hash would not filter out collisions):
	return fHashVal != right.fHashVal
		|| fString != right.fString
		|| fContext != right.fContext
		|| fComment != right.fComment;
}


status_t
CatKey::GetStringParts(BString* str, BString* ctx, BString* cmt) const
{
	if (str) *str = fString;
	if (ctx) *ctx = fContext;
	if (cmt) *cmt = fComment;

	return B_OK;
}


uint32 CatKey::HashFun(const char* s, int startValue) {
	unsigned long h = startValue;
	for ( ; *s; ++s)
		h = 5 * h + *s;

	// Add 1 to differenciate ("ab","cd","ef") from ("abcd","e","f")
	h = 5 * h + 1;

	return size_t(h);
}

// (end CatKey)
// BHashMapCatalog


void
BHashMapCatalog::MakeEmpty()
{
	fCatMap.Clear();
}


int32
BHashMapCatalog::CountItems() const
{
	return fCatMap.Size();
}


const char *
BHashMapCatalog::GetString(const char *string, const char *context,
	const char *comment)
{
	CatKey key(string, context, comment);
	return GetString(key);
}


const char *
BHashMapCatalog::GetString(uint32 id)
{
	CatKey key(id);
	return GetString(key);
}


const char *
BHashMapCatalog::GetString(const CatKey& key)
{
	BString value = fCatMap.Get(key);
	if (value.Length() == 0)
		return NULL;
	else
		return value.String();
}


void
parseQuotedChars(BString& stringToParse)
{
	char* in = stringToParse.LockBuffer(0);
	char* out = in;
	int newLength = 0;
	bool quoted = false;

	while (*in != 0 || quoted) {
		if (quoted) {
			if (*in == 'n')
				*out = '\n';
			else if (*in == 't')
				*out = '\t';
			else if (*in == '"')
				*out = '"';
			else if (*in == 'x') {
				// Parse the 2-digit hex integer that follows
				unsigned int hexchar = strtoul(in + 1, NULL, 16);
				*out = hexchar;
				// skip the number
				in += 2;
			} else {
				// dump quote from unknown quoting-sequence:
				*out = *in ;
			}
			quoted = false;
			out++;
			newLength++;
		} else {
			quoted = (*in == '\\');
			if (!quoted) {
				*out = *in;
				out++;
				newLength++;
			}
		}
		in++;
	}
	*out = '\0';
	stringToParse.UnlockBuffer();
}


status_t
BHashMapCatalog::SetString(const char *string, const char *translated,
	const char *context, const char *comment)
{
	BString stringCopy(string);
	BString translatedCopy(translated);
	parseQuotedChars(stringCopy);
	parseQuotedChars(translatedCopy);
	CatKey key(stringCopy.String(), context, comment);
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


status_t
BHashMapCatalog::SetString(int32 id, const char *translated)
{
	BString translatedCopy(translated);
	parseQuotedChars(translatedCopy);
	CatKey key(id);
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


status_t
BHashMapCatalog::SetString(const CatKey& key, const char *translated)
{
	BString translatedCopy(translated);
	parseQuotedChars(translatedCopy);
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


/*
 * computes a checksum (we call it fingerprint) on all the catalog-keys. We do
 * not include the values, since we want catalogs for different languages of the
 * same app to have the same fingerprint, since we use it to separate different
 * catalog-versions. We use a simple sum because there is no well known
 * checksum algorithm that gives the same result if the string are sorted in the
 * wrong order, and this does happen, as an hash map is an unsorted container.
 */
uint32
BHashMapCatalog::ComputeFingerprint() const
{
	uint32 checksum = 0;

	int32 hash;
	CatMap::Iterator iter = fCatMap.GetIterator();
	CatMap::Entry entry;
	while (iter.HasNext())
	{
		entry = iter.Next();
		hash = entry.key.fHashVal;
		checksum += hash;
	}
	return checksum;
}


void
BHashMapCatalog::UpdateFingerprint()
{
	fFingerprint = ComputeFingerprint();
}
