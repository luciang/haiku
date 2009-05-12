/*
 * Copyright 2004-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Sandor Vroemisse
 *		Jérôme Duval
 *		Axel Dörfler, axeld@pinc-software.de.
 */

#include "Keymap.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <ByteOrder.h>
#include <File.h>

#include <input_globals.h>


static const uint32 kModifierKeys = B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY
	| B_CAPS_LOCK | B_OPTION_KEY | B_MENU_KEY;


static void
print_key(char *chars, int32 offset)
{
	int size = chars[offset++];

	switch (size) {
		case 0:
			// Not mapped
			printf("N/A");
			break;

		case 1:
			// 1-byte UTF-8/ASCII character
			printf("%c", chars[offset]);
			break;

		default:
		{
			// 2-, 3-, or 4-byte UTF-8 character
			char *str = new char[size + 1];
			strncpy(str, &chars[offset], size);
			str[size] = 0;
			printf("%s", str);
			delete[] str;
			break;
		}
	}

	printf("\t");
}


//	#pragma mark -


Keymap::Keymap()
	:
	fChars(NULL),
	fCharsSize(0),
	fModificationMessage(NULL)
{
}


Keymap::~Keymap()
{
	delete fModificationMessage;
}


void
Keymap::SetTarget(BMessenger target, BMessage* modificationMessage)
{
	delete fModificationMessage;

	fTarget = target;
	fModificationMessage = modificationMessage;
}


void
Keymap::SetName(const char* name)
{
	strlcpy(fName, name, sizeof(fName));
}


void
Keymap::DumpKeymap()
{
	// Print a chart of the normal, shift, option, and option+shift
	// keys.
	printf("Key #\tNormal\tShift\tCaps\tC+S\tOption\tO+S\tO+C\tO+C+S\tControl\n");
	for (int i = 0; i < 128; i++) {
		printf(" 0x%x\t", i);
		print_key(fChars, fKeys.normal_map[i]);
		print_key(fChars, fKeys.shift_map[i]);
		print_key(fChars, fKeys.caps_map[i]);
		print_key(fChars, fKeys.caps_shift_map[i]);
		print_key(fChars, fKeys.option_map[i]);
		print_key(fChars, fKeys.option_shift_map[i]);
		print_key(fChars, fKeys.option_caps_map[i]);
		print_key(fChars, fKeys.option_caps_shift_map[i]);
		print_key(fChars, fKeys.control_map[i]);
		printf("\n");
	}
}


/*
	file format in big endian :
	struct key_map
	uint32 size of following charset
	charset (offsets go into this with size of character followed by character)
*/
// we load a map from a file
status_t
Keymap::Load(entry_ref &ref)
{
	status_t err;
	BEntry entry(&ref, true);
	if ((err = entry.InitCheck()) != B_OK) {
		fprintf(stderr, "error loading keymap: %s\n", strerror(err));
		return err;
	}

	BFile file(&entry, B_READ_ONLY);
	if ((err = file.InitCheck()) != B_OK) {
		fprintf(stderr, "error loading keymap: %s\n", strerror(err));
		return err;
	}

	if ((err = file.Read(&fKeys, sizeof(fKeys))) < (ssize_t)sizeof(fKeys)) {
		fprintf(stderr, "error reading keymap keys: %s\n", strerror(err));
		return B_BAD_VALUE;
	}

	for (uint32 i=0; i<sizeof(fKeys)/4; i++)
		((uint32*)&fKeys)[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)&fKeys)[i]);

	if ((err = file.Read(&fCharsSize, sizeof(uint32))) < (ssize_t)sizeof(uint32)) {
		fprintf(stderr, "error reading keymap size: %s\n", strerror(err));
		return B_BAD_VALUE;
	}

	fCharsSize = B_BENDIAN_TO_HOST_INT32(fCharsSize);
	delete[] fChars;

	fChars = new char[fCharsSize];

	err = file.Read(fChars, fCharsSize);
	if (err < B_OK) {
		fprintf(stderr, "error reading keymap chars: %s\n", strerror(err));
	}

	// fetch name from attribute and fall back to filename
	ssize_t bytesRead
		= file.ReadAttr("keymap:name", B_STRING_TYPE, 0, fName, sizeof(fName));
	if (bytesRead > 0)
		fName[bytesRead] = '\0';
	else
		strlcpy(fName, ref.name, sizeof(fName));
	return err;
}


//!	We save a map to a file
status_t
Keymap::Save(entry_ref& ref)
{
	BFile file;
	status_t status = file.SetTo(&ref,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK) {
		printf("error %s\n", strerror(status));
		return status;
	}

	for (uint32 i = 0; i < sizeof(fKeys) / 4; i++)
		((uint32*)&fKeys)[i] = B_HOST_TO_BENDIAN_INT32(((uint32*)&fKeys)[i]);

	ssize_t bytesWritten = file.Write(&fKeys, sizeof(fKeys));
	if (bytesWritten < (ssize_t)sizeof(fKeys))
		status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;

	for (uint32 i = 0; i < sizeof(fKeys) / 4; i++)
		((uint32*)&fKeys)[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)&fKeys)[i]);

	if (status == B_OK) {
		fCharsSize = B_HOST_TO_BENDIAN_INT32(fCharsSize);

		bytesWritten = file.Write(&fCharsSize, sizeof(uint32));
		if (bytesWritten < (ssize_t)sizeof(uint32))
			status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;

		fCharsSize = B_BENDIAN_TO_HOST_INT32(fCharsSize);
	}

	if (status == B_OK) {
		bytesWritten = file.Write(fChars, fCharsSize);
		if (bytesWritten < (ssize_t)fCharsSize)
			status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;
	}

	if (status == B_OK) {
		file.WriteAttr("keymap:name", B_STRING_TYPE, 0, fName, strlen(fName));
			// Failing would be non-fatal
	}

	return status;
}


bool
Keymap::Equals(const Keymap& other) const
{
	// not really efficient but this is the only way i found
	// to reliably compare keymaps (used only for apply and revert)
	return fCharsSize == other.fCharsSize
		&& !memcmp(&other.fKeys, &fKeys, sizeof(key_map))
		&& !memcmp(other.fChars, fChars, fCharsSize);
}


/*!	We need to know if a key is a modifier key to choose
	a valid key when several are pressed together.
*/
bool
Keymap::IsModifierKey(uint32 keyCode)
{
	return keyCode == fKeys.caps_key
		|| keyCode == fKeys.num_key
		|| keyCode == fKeys.scroll_key
		|| keyCode == fKeys.left_shift_key
		|| keyCode == fKeys.right_shift_key
		|| keyCode == fKeys.left_command_key
		|| keyCode == fKeys.right_command_key
		|| keyCode == fKeys.left_control_key
		|| keyCode == fKeys.right_control_key
		|| keyCode == fKeys.left_option_key
		|| keyCode == fKeys.right_option_key
		|| keyCode == fKeys.menu_key;
}


//! We need to know a modifier for a key
uint32
Keymap::Modifier(uint32 keyCode)
{
	if (keyCode == fKeys.caps_key)
		return B_CAPS_LOCK;
	if (keyCode == fKeys.num_key)
		return B_NUM_LOCK;
	if (keyCode == fKeys.scroll_key)
		return B_SCROLL_LOCK;
	if (keyCode == fKeys.left_shift_key)
		return B_LEFT_SHIFT_KEY | B_SHIFT_KEY;
	if (keyCode == fKeys.right_shift_key)
		return B_RIGHT_SHIFT_KEY | B_SHIFT_KEY;
	if (keyCode == fKeys.left_command_key)
		return B_LEFT_COMMAND_KEY | B_COMMAND_KEY;
	if (keyCode == fKeys.right_command_key)
		return B_RIGHT_COMMAND_KEY | B_COMMAND_KEY;
	if (keyCode == fKeys.left_control_key)
		return B_LEFT_CONTROL_KEY | B_CONTROL_KEY;
	if (keyCode == fKeys.right_control_key)
		return B_RIGHT_CONTROL_KEY | B_CONTROL_KEY;
	if (keyCode == fKeys.left_option_key)
		return B_LEFT_OPTION_KEY | B_OPTION_KEY;
	if (keyCode == fKeys.right_option_key)
		return B_RIGHT_OPTION_KEY | B_OPTION_KEY;
	if (keyCode == fKeys.menu_key)
		return B_MENU_KEY;

	return 0;
}


uint32
Keymap::KeyForModifier(uint32 modifier)
{
	if (modifier == B_CAPS_LOCK)
		return fKeys.caps_key;
	if (modifier == B_NUM_LOCK)
		return fKeys.num_key;
	if (modifier == B_SCROLL_LOCK)
		return fKeys.scroll_key;
	if (modifier == B_LEFT_SHIFT_KEY || modifier == B_SHIFT_KEY)
		return fKeys.left_shift_key;
	if (modifier == B_RIGHT_SHIFT_KEY)
		return fKeys.right_shift_key;
	if (modifier == B_LEFT_COMMAND_KEY || modifier == B_COMMAND_KEY)
		return fKeys.left_command_key;
	if (modifier == B_RIGHT_COMMAND_KEY)
		return fKeys.right_command_key;
	if (modifier == B_LEFT_CONTROL_KEY || modifier == B_CONTROL_KEY)
		return fKeys.left_control_key;
	if (modifier == B_RIGHT_CONTROL_KEY)
		return fKeys.right_control_key;
	if (modifier == B_LEFT_OPTION_KEY || modifier == B_OPTION_KEY)
		return fKeys.left_option_key;
	if (modifier == B_RIGHT_OPTION_KEY)
		return fKeys.right_option_key;
	if (modifier == B_MENU_KEY)
		return fKeys.menu_key;

	return 0;
}


status_t
Keymap::SetModifier(uint32 keyCode, uint32 modifier)
{
	const uint32 kSingleKeys = B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY
		| B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY | B_LEFT_CONTROL_KEY
		| B_RIGHT_CONTROL_KEY | B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY;

	if ((modifier & kSingleKeys) != 0)
		modifier &= kSingleKeys;
	else if ((modifier & kModifierKeys) != 0)
		modifier &= kModifierKeys;

	if (modifier == B_CAPS_LOCK)
		fKeys.caps_key = keyCode;
	else if (modifier == B_NUM_LOCK)
		fKeys.num_key = keyCode;
	else if (modifier == B_SCROLL_LOCK)
		fKeys.scroll_key = keyCode;
	else if (modifier == B_LEFT_SHIFT_KEY)
		fKeys.left_shift_key = keyCode;
	else if (modifier == B_RIGHT_SHIFT_KEY)
		fKeys.right_shift_key = keyCode;
	else if (modifier == B_LEFT_COMMAND_KEY)
		fKeys.left_command_key = keyCode;
	else if (modifier == B_RIGHT_COMMAND_KEY)
		fKeys.right_command_key = keyCode;
	else if (modifier == B_LEFT_CONTROL_KEY)
		fKeys.left_control_key = keyCode;
	else if (modifier == B_RIGHT_CONTROL_KEY)
		fKeys.right_control_key = keyCode;
	else if (modifier == B_LEFT_OPTION_KEY)
		fKeys.left_option_key = keyCode;
	else if (modifier == B_RIGHT_OPTION_KEY)
		fKeys.right_option_key = keyCode;
	else if (modifier == B_MENU_KEY)
		fKeys.menu_key = keyCode;
	else
		return B_BAD_VALUE;

	if (fModificationMessage != NULL)
		fTarget.SendMessage(fModificationMessage);

	return B_OK;
}


/*! Checks whether a key is a dead key.
	If it is, the enabled/disabled state of that dead key will be passed
	out via isEnabled (isEnabled is not touched for non-dead keys).
*/
uint8
Keymap::IsDeadKey(uint32 keyCode, uint32 modifiers, bool* isEnabled)
{
	uint32 tableMask = 0;
	int32 offset = _Offset(keyCode, modifiers, &tableMask);
	uint8 deadKeyIndex = _GetDeadKeyIndex(offset);
	if (deadKeyIndex > 0 && isEnabled != NULL) {
		uint32 deadTables[] = {
			fKeys.acute_tables,
			fKeys.grave_tables,
			fKeys.circumflex_tables,
			fKeys.dieresis_tables,
			fKeys.tilde_tables
		};
		*isEnabled = (deadTables[deadKeyIndex - 1] & tableMask) != 0;
	}

	return deadKeyIndex;
}


//! Tell if a key is a dead second key, needed for draw a dead second key.
bool
Keymap::IsDeadSecondKey(uint32 keyCode, uint32 modifiers, uint8 activeDeadKey)
{
	if (!activeDeadKey)
		return false;

	int32 offset = _Offset(keyCode, modifiers);
	if (offset < 0)
		return false;

	uint32 numBytes = fChars[offset];
	if (!numBytes)
		return false;

	int32* deadOffsets[] = {
		fKeys.acute_dead_key,
		fKeys.grave_dead_key,
		fKeys.circumflex_dead_key,
		fKeys.dieresis_dead_key,
		fKeys.tilde_dead_key
	};

	int32 *deadOffset = deadOffsets[activeDeadKey - 1];

	for (int32 i=0; i<32; i++) {
		if (offset == deadOffset[i])
			return true;

		uint32 deadNumBytes = fChars[deadOffset[i]];

		if (!deadNumBytes)
			continue;

		if (strncmp(&fChars[offset + 1], &fChars[deadOffset[i] + 1],
				deadNumBytes) == 0)
			return true;
		i++;
	}
	return false;
}


//! Enables/disables the "deadness" of the given keycode/modifier combo.
void
Keymap::SetDeadKeyEnabled(uint32 keyCode, uint32 modifiers, bool enabled)
{
	uint32 tableMask = 0;
	int32 offset = _Offset(keyCode, modifiers, &tableMask);
	uint8 deadKeyIndex = _GetDeadKeyIndex(offset);
	if (deadKeyIndex > 0) {
		uint32* deadTables[] = {
			&fKeys.acute_tables,
			&fKeys.grave_tables,
			&fKeys.circumflex_tables,
			&fKeys.dieresis_tables,
			&fKeys.tilde_tables
		};

		if (enabled)
			(*deadTables[deadKeyIndex - 1]) |= tableMask;
		else
			(*deadTables[deadKeyIndex - 1]) &= ~tableMask;

		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


/*! Returns the trigger character string that is currently set for the dead
	key with the given index (which is 1..5).
*/
void
Keymap::GetDeadKeyTrigger(dead_key_index deadKeyIndex, BString& outTrigger)
{
	outTrigger = "";
	if (deadKeyIndex < 1 || deadKeyIndex > 5)
		return;

	int32 deadOffsets[] = {
		fKeys.acute_dead_key[1],
		fKeys.grave_dead_key[1],
		fKeys.circumflex_dead_key[1],
		fKeys.dieresis_dead_key[1],
		fKeys.tilde_dead_key[1]
	};

	int32 offset = deadOffsets[deadKeyIndex - 1];
	if (offset < 0 || offset >= (int32)fCharsSize)
		return;

	uint32 deadNumBytes = fChars[offset];
	if (!deadNumBytes)
		return;

	outTrigger.SetTo(&fChars[offset + 1], deadNumBytes);
}


/*! Sets the trigger character string that shall be used for the dead key
	with the given index (which is 1..5).
*/
void
Keymap::SetDeadKeyTrigger(dead_key_index deadKeyIndex, const BString& trigger)
{
	if (deadKeyIndex < 1 || deadKeyIndex > 5)
		return;

	int32 deadOffsets[] = {
		fKeys.acute_dead_key[1],
		fKeys.grave_dead_key[1],
		fKeys.circumflex_dead_key[1],
		fKeys.dieresis_dead_key[1],
		fKeys.tilde_dead_key[1]
	};

	int32 offset = deadOffsets[deadKeyIndex - 1];
	if (offset < 0 || offset >= (int32)fCharsSize)
		return;

	if (_SetChars(offset, trigger.String(), trigger.Length())) {
		// reset modifier table such that new dead key is enabled wherever
		// it is available
		uint32* deadTables[] = {
			&fKeys.acute_tables,
			&fKeys.grave_tables,
			&fKeys.circumflex_tables,
			&fKeys.dieresis_tables,
			&fKeys.tilde_tables
		};
		*deadTables[deadKeyIndex - 1]
			= B_CONTROL_TABLE | B_OPTION_CAPS_SHIFT_TABLE | B_OPTION_CAPS_TABLE
				| B_OPTION_SHIFT_TABLE | B_OPTION_TABLE | B_CAPS_SHIFT_TABLE
				| B_CAPS_TABLE | B_SHIFT_TABLE | B_NORMAL_TABLE;

		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


//! Get the char for a key given modifiers and active dead key
void
Keymap::GetChars(uint32 keyCode, uint32 modifiers, uint8 activeDeadKey,
	char** chars, int32* numBytes)
{
	*numBytes = 0;
	*chars = NULL;

	if (keyCode > 128 || fChars == NULL)
		return;

	// here we take NUMLOCK into account
	if ((modifiers & B_NUM_LOCK) != 0) {
		switch (keyCode) {
			case 0x37:
			case 0x38:
			case 0x39:
			case 0x48:
			case 0x49:
			case 0x4a:
			case 0x58:
			case 0x59:
			case 0x5a:
			case 0x64:
			case 0x65:
				modifiers ^= B_SHIFT_KEY;
		}
	}

	int32 offset = _Offset(keyCode, modifiers);
	if (offset < 0)
		return;

	// here we get the char size
	*numBytes = fChars[offset];
	if (!*numBytes)
		return;

	// here we take an potential active dead key
	int32 *deadKey;
	switch (activeDeadKey) {
		case kDeadKeyAcute:
			deadKey = fKeys.acute_dead_key;
			break;
		case kDeadKeyGrave:
			deadKey = fKeys.grave_dead_key;
			break;
		case kDeadKeyCircumflex:
			deadKey = fKeys.circumflex_dead_key;
			break;
		case kDeadKeyDiaeresis:
			deadKey = fKeys.dieresis_dead_key;
			break;
		case kDeadKeyTilde:
			deadKey = fKeys.tilde_dead_key;
			break;
		default:
		{
			// if not dead, we copy and return the char
			char *str = *chars = new char[*numBytes + 1];
			strncpy(str, &fChars[offset + 1], *numBytes);
			str[*numBytes] = 0;
			return;
		}
	}

	// if dead key, we search for our current offset char in the dead key
	// offset table string comparison is needed
	for (int32 i = 0; i < 32; i++) {
		if (strncmp(&fChars[offset + 1], &fChars[deadKey[i] + 1], *numBytes)
				== 0) {
			*numBytes = fChars[deadKey[i + 1]];

			switch (*numBytes) {
				case 0:
					// Not mapped
					*chars = NULL;
					break;
				default:
				{
					// 1-, 2-, 3-, or 4-byte UTF-8 character
					char *str = *chars = new char[*numBytes + 1];
					strncpy(str, &fChars[deadKey[i + 1] + 1], *numBytes);
					str[*numBytes] = 0;
					break;
				}
			}
			return;
		}
		i++;
	}

	// if not found we return the current char mapped
	*chars = new char[*numBytes + 1];
	strncpy(*chars, &fChars[offset + 1], *numBytes);
	(*chars)[*numBytes] = 0;
}


//! We make our input server use the map in /boot/home/config/settings/Keymap
status_t
Keymap::Use()
{
	return _restore_key_map_();
}


void
Keymap::SetKey(uint32 keyCode, uint32 modifiers, int8 deadKey,
	const char* bytes, int32 numBytes)
{
	int32 offset = _Offset(keyCode, modifiers);
	if (offset < 0)
		return;

	if (numBytes == -1)
		numBytes = strlen(bytes);
	if (numBytes > 6)
		return;

	if (_SetChars(offset, bytes, numBytes)) {
		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


Keymap&
Keymap::operator=(const Keymap& other)
{
	if (this == &other)
		return *this;

	delete[] fChars;
	delete fModificationMessage;

	fChars = new(std::nothrow) char[other.fCharsSize];
	if (fChars != NULL) {
		memcpy(fChars, other.fChars, other.fCharsSize);
		fCharsSize = other.fCharsSize;
	} else
		fCharsSize = 0;

	memcpy(&fKeys, &other.fKeys, sizeof(key_map));
	strlcpy(fName, other.fName, sizeof(fName));

	fTarget = other.fTarget;

	if (other.fModificationMessage != NULL)
		fModificationMessage = new BMessage(*other.fModificationMessage);

	return *this;
}


int32
Keymap::_Offset(uint32 keyCode, uint32 modifiers, uint32* _table)
{
	int32 offset;
	uint32 table;

	if (keyCode >= 128)
		return -1;

	switch (modifiers & kModifierKeys) {
		case B_SHIFT_KEY:
			offset = fKeys.shift_map[keyCode];
			table = B_SHIFT_TABLE;
			break;
		case B_CAPS_LOCK:
			offset = fKeys.caps_map[keyCode];
			table = B_CAPS_TABLE;
			break;
		case B_CAPS_LOCK | B_SHIFT_KEY:
			offset = fKeys.caps_shift_map[keyCode];
			table = B_CAPS_SHIFT_TABLE;
			break;
		case B_OPTION_KEY:
			offset = fKeys.option_map[keyCode];
			table = B_OPTION_TABLE;
			break;
		case B_OPTION_KEY | B_SHIFT_KEY:
			offset = fKeys.option_shift_map[keyCode];
			table = B_OPTION_SHIFT_TABLE;
			break;
		case B_OPTION_KEY | B_CAPS_LOCK:
			offset = fKeys.option_caps_map[keyCode];
			table = B_OPTION_CAPS_TABLE;
			break;
		case B_OPTION_KEY | B_SHIFT_KEY | B_CAPS_LOCK:
			offset = fKeys.option_caps_shift_map[keyCode];
			table = B_OPTION_CAPS_SHIFT_TABLE;
			break;
		case B_CONTROL_KEY:
			offset = fKeys.control_map[keyCode];
			table = B_CONTROL_TABLE;
			break;
		default:
			offset = fKeys.normal_map[keyCode];
			table = B_NORMAL_TABLE;
			break;
	}

	if (_table != NULL)
		*_table = table;

	if (offset >= (int32)fCharsSize)
		return -1;

	return offset;
}


bool
Keymap::_SetChars(int32 offset, const char* bytes, int32 numBytes)
{
	int32 oldNumBytes = fChars[offset];

	if (oldNumBytes == numBytes
		&& !memcmp(&fChars[offset + 1], bytes, numBytes)) {
		// nothing to do
		return false;
	}

	int32 diff = numBytes - oldNumBytes;
	if (diff != 0) {
		fCharsSize += diff;

		if (diff > 0) {
			// make space for the new data
			char* chars = new(std::nothrow) char[fCharsSize];
			if (chars != NULL) {
				memcpy(chars, fChars, offset + oldNumBytes + 1);
				memcpy(&chars[offset + 1 + numBytes],
					&fChars[offset + 1 + oldNumBytes],
					fCharsSize - 2 - offset - diff);
				delete[] fChars;
				fChars = chars;
			} else
				return false;
		} else if (diff < 0) {
			// shrink table
			memmove(&fChars[offset + numBytes], &fChars[offset + oldNumBytes],
				fCharsSize - offset - 2 - diff);
		}

		// update offsets
		int32* data = fKeys.control_map;
		int32 size = sizeof(fKeys.control_map) / 4 * 9
			+ sizeof(fKeys.acute_dead_key) / 4 * 5;
		for (int32 i = 0; i < size; i++) {
			if (data[i] > offset)
				data[i] += diff;
		}
	}

	memcpy(&fChars[offset + 1], bytes, numBytes);
	fChars[offset] = numBytes;

	return true;
}


uint8
Keymap::_GetDeadKeyIndex(int32 offset)
{
	if (fChars == NULL || offset <= 0)
		return 0;

	uint32 numBytes = fChars[offset];
	if (!numBytes)
		return 0;

	char chars[4];
	strncpy(chars, &fChars[offset + 1], numBytes);
	chars[numBytes] = 0;

	int32 deadOffsets[] = {
		fKeys.acute_dead_key[1],
		fKeys.grave_dead_key[1],
		fKeys.circumflex_dead_key[1],
		fKeys.dieresis_dead_key[1],
		fKeys.tilde_dead_key[1]
	};

	uint8 result = 0;
	for (int32 i = 0; i < 5; i++) {
		if (offset == deadOffsets[i]) {
			result = i + 1;
			break;
		}

		uint32 deadNumBytes = fChars[deadOffsets[i]];
		if (!deadNumBytes)
			continue;

		if (strncmp(chars, &fChars[deadOffsets[i] + 1], deadNumBytes) == 0) {
			result = i + 1;
			break;
		}
	}

	return result;
}
