/*
 * Copyright (c) 2001-2008, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Erik Jaesler (erik@cgsoftware.com)
 */

/*!	BArchivable mix-in class defines the archiving protocol.
	Also some global archiving functions.
*/


#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <syslog.h>
#include <typeinfo>
#include <vector>

#include <AppFileInfo.h>
#include <Archivable.h>
#include <Entry.h>
#include <List.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>


using std::string;
using std::vector;

const char* B_CLASS_FIELD = "class";
const char* B_ADD_ON_FIELD = "add_on";
const int32 FUNC_NAME_LEN = 1024;

// TODO: consider moving these to a separate module, and making them more
//	full-featured (e.g., taking NS::ClassName::Function(Param p) instead
//	of just NS::ClassName)


static status_t
demangle_class_name(const char* name, BString& out)
{
// TODO: add support for template classes
//	_find__t12basic_string3ZcZt18string_char_traits1ZcZt24__default_alloc_template2b0i0PCccUlUl

	out = "";

	if (name[0] == 'Q') {
		// The name is in a namespace
		int namespaceCount = 0;
		name++;
		if (name[0] == '_') {
			// more than 10 namespaces deep
			if (!isdigit(*++name))
				return B_BAD_VALUE;

			namespaceCount = strtoul(name, (char**)&name, 10);
			if (name[0] != '_')
				return B_BAD_VALUE;	
		} else
			namespaceCount = name[0] - '0';

		name++;

		for (int i = 0; i < namespaceCount - 1; i++) {
			if (!isdigit(name[0]))
				return B_BAD_VALUE;	

			int nameLength = strtoul(name, (char**)&name, 10);
			out.Append(name, nameLength);
			out += "::";
			name += nameLength;
		}
	}

	int nameLength = strtoul(name, (char**)&name, 10);
	out.Append(name, nameLength);

	return B_OK;
}


static void
mangle_class_name(const char* name, BString& out)
{
// TODO: add support for template classes
//	_find__t12basic_string3ZcZt18string_char_traits1ZcZt24__default_alloc_template2b0i0PCccUlUl

	//	Chop this:
	//		testthree::testfour::Testthree::Testfour
	//	up into little bite-sized pieces
	int count = 0;
	string origName(name);
	vector<string> spacenames;

	string::size_type pos = 0;
	string::size_type oldpos = 0;
	while (pos != string::npos) {
		pos = origName.find_first_of("::", oldpos);
		spacenames.push_back(string(origName, oldpos, pos - oldpos));
		pos = origName.find_first_not_of("::", pos);
		oldpos = pos;
		++count;
	}

	//	Now mangle it into this:
	//		Q49testthree8testfour9Testthree8Testfour
	out = "";
	if (count > 1) {
		out += 'Q';
		if (count > 10)
			out += '_';
		out << count;
		if (count > 10)
			out += '_';
	}

	for (unsigned int i = 0; i < spacenames.size(); ++i) {
		out << (int)spacenames[i].length();
		out += spacenames[i].c_str();
	}
}


static void
build_function_name(const BString& className, BString& funcName)
{
	funcName = "";

	//	This is what we're after:
	//		Instantiate__Q28OpenBeOS11BArchivableP8BMessage
	mangle_class_name(className.String(), funcName);
#if __GNUC__ >= 4
	funcName.Prepend("_ZN");
	funcName.Append("11InstantiateE");
#else
	funcName.Prepend("Instantiate__");
#endif
	funcName.Append("P8BMessage");
}


static bool
add_private_namespace(BString& name)
{
	if (name.Compare("_", 1) != 0)
		return false;

	name.Prepend("BPrivate::");
	return true;
}


static instantiation_func
find_function_in_image(BString& funcName, image_id id, status_t& err)
{
	instantiation_func instantiationFunc = NULL;
	err = get_image_symbol(id, funcName.String(), B_SYMBOL_TYPE_TEXT,
		(void**)&instantiationFunc);
	if (err != B_OK)
		return NULL;

	return instantiationFunc;
}


static status_t
check_signature(const char* signature, image_info& info)
{
	if (signature == NULL) {
		// If it wasn't specified, anything "matches"
		return B_OK;
	}

	// Get image signature
	BFile file(info.name, B_READ_ONLY);
	status_t err = file.InitCheck();
	if (err != B_OK)
		return err;

	char imageSignature[B_MIME_TYPE_LENGTH];
	BAppFileInfo appFileInfo(&file);
	err = appFileInfo.GetSignature(imageSignature);
	if (err != B_OK) {
		syslog(LOG_ERR, "instantiate_object - couldn't get mime sig for %s",
			info.name);
		return err;
	}

	if (strcmp(signature, imageSignature))
		return B_MISMATCHED_VALUES;

	return B_OK;
}


//	#pragma mark -


BArchivable::BArchivable()
{
}


BArchivable::BArchivable(BMessage* from)
{
}


BArchivable::~BArchivable()
{
}


status_t
BArchivable::Archive(BMessage* into, bool deep) const
{
	if (!into) {
		// TODO: logging/other error reporting?
		return B_BAD_VALUE;
	}

	BString name;
	status_t status = demangle_class_name(typeid(*this).name(), name);
	if (status != B_OK)
		return status;

	return into->AddString(B_CLASS_FIELD, name);
}


BArchivable*
BArchivable::Instantiate(BMessage* from)
{
	debugger("Can't create a plain BArchivable object");
	return NULL;
}


status_t
BArchivable::Perform(perform_code d, void* arg)
{
	// TODO: Check against original
	return B_ERROR;
}


void BArchivable::_ReservedArchivable1() {}
void BArchivable::_ReservedArchivable2() {}
void BArchivable::_ReservedArchivable3() {}


// #pragma mark -


BArchivable*
instantiate_object(BMessage* archive, image_id* _id)
{
	status_t statusBuffer;
	status_t* status = &statusBuffer;
	if (_id != NULL)
		status = _id;

	// Check our params
	if (archive == NULL) {
		syslog(LOG_ERR, "instantiate_object failed: NULL BMessage argument");
		*status = B_BAD_VALUE;
		return NULL;
	}

	// Get class name from archive
	const char* className = NULL;
	status_t err = archive->FindString(B_CLASS_FIELD, &className);
	if (err) {
		syslog(LOG_ERR, "instantiate_object failed: Failed to find an entry "
			"defining the class name (%s).", strerror(err));
		*status = B_BAD_VALUE;
		return NULL;
	}

	// Get sig from archive
	const char* signature = NULL;
	bool hasSignature = archive->FindString(B_ADD_ON_FIELD, &signature) == B_OK;

	instantiation_func instantiationFunc = find_instantiation_func(className,
		signature);

	// if find_instantiation_func() can't locate Class::Instantiate()
	// and a signature was specified
	if (!instantiationFunc && hasSignature) {
		// use BRoster::FindApp() to locate an app or add-on with the symbol
		BRoster Roster;
		entry_ref ref;
		err = Roster.FindApp(signature, &ref);

		// if an entry_ref is obtained
		BEntry entry;
		if (err == B_OK)
			err = entry.SetTo(&ref);

		BPath path;
		if (err == B_OK)
			err = entry.GetPath(&path);

		if (err != B_OK) {
			syslog(LOG_ERR, "instantiate_object failed: Error finding app "
				"with signature \"%s\" (%s)", signature, strerror(err));
			*status = err;
			return NULL;
		}

		// load the app/add-on
		image_id addOn = load_add_on(path.Path());
		if (addOn < B_OK) {
			syslog(LOG_ERR, "instantiate_object failed: Could not load "
				"add-on %s: %s.", path.Path(), strerror(addOn));
			*status = addOn;
			return NULL;
		}

		// Save the image_id
		if (_id != NULL)
			*_id = addOn;

		BString name = className;
		for (int32 pass = 0; pass < 2; pass++) {
			BString funcName;
			build_function_name(name, funcName);

			instantiationFunc = find_function_in_image(funcName, addOn, err);
			if (instantiationFunc != NULL)
				break;

			// Check if we have a private class, and add the BPrivate namespace
			// (for backwards compatibility)
			if (!add_private_namespace(name))
				break;
		}

		if (instantiationFunc == NULL) {
			syslog(LOG_ERR, "instantiate_object failed: Failed to find exported "
				"Instantiate static function for class %s.", className);
			*status = B_NAME_NOT_FOUND;
			return NULL;
		}
	} else if (instantiationFunc == NULL) {
		syslog(LOG_ERR, "instantiate_object failed: No signature specified "
			"in archive, looking for class \"%s\".", className);
		*status = B_NAME_NOT_FOUND;
		return NULL;
	}

	// if Class::Instantiate(BMessage*) was found
	if (instantiationFunc != NULL) {
		// use to create and return an object instance
		return instantiationFunc(archive);
	}

	return NULL;
}


BArchivable*
instantiate_object(BMessage* from)
{
	return instantiate_object(from, NULL);
}


bool
validate_instantiation(BMessage* from, const char* className)
{
	// Make sure our params are kosher -- original skimped here =P
	if (!from) {
		errno = B_BAD_VALUE;
		return false;
	}

	BString name = className;
	for (int32 pass = 0; pass < 2; pass++) {
		const char* archiveClassName;
		for (int32 index = 0; from->FindString(B_CLASS_FIELD, index,
				&archiveClassName) == B_OK; ++index) {
			if (name == archiveClassName)
				return true;
		}

		if (!add_private_namespace(name))
			break;
	}

	errno = B_MISMATCHED_VALUES;
	syslog(LOG_ERR, "validate_instantiation failed on class %s.", className);

	return false;
}


instantiation_func
find_instantiation_func(const char* className, const char* signature)
{
	if (className == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	thread_info threadInfo;
	status_t err = get_thread_info(find_thread(NULL), &threadInfo);
	if (err != B_OK) {
		errno = err;
		return NULL;
	}

	instantiation_func instantiationFunc = NULL;
	image_info imageInfo;

	BString name = className;
	for (int32 pass = 0; pass < 2; pass++) {
		BString funcName;
		build_function_name(name, funcName);

		// for each image_id in team_id
		int32 cookie = 0;
		while (instantiationFunc == NULL
			&& get_next_image_info(threadInfo.team, &cookie, &imageInfo)
				== B_OK) {
			instantiationFunc = find_function_in_image(funcName, imageInfo.id,
				err);
		}
		if (instantiationFunc != NULL)
			break;

		// Check if we have a private class, and add the BPrivate namespace
		// (for backwards compatibility)
		if (!add_private_namespace(name))
			break;
	}

	if (instantiationFunc != NULL
		&& check_signature(signature, imageInfo) != B_OK)
		return NULL;

	return instantiationFunc;
}


instantiation_func
find_instantiation_func(const char* className)
{
	return find_instantiation_func(className, NULL);
}


instantiation_func
find_instantiation_func(BMessage* archive)
{
	if (archive == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	const char* name = NULL;
	const char* signature = NULL;
	if (archive->FindString(B_CLASS_FIELD, &name) != B_OK
		|| archive->FindString(B_ADD_ON_FIELD, &signature)) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	return find_instantiation_func(name, signature);
}

