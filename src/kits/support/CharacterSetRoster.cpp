#include <CharacterSet.h>
#include <CharacterSetRoster.h>
#include "character_sets.h"

namespace BPrivate {

BCharacterSetRoster::BCharacterSetRoster()
{
	index = 0;
}

BCharacterSetRoster::~BCharacterSetRoster()
{
	// nothing to do
}

status_t
BCharacterSetRoster::GetNextCharacterSet(BCharacterSet * charset)
{
	if (charset == 0) {
		return B_BAD_VALUE;
	}
	if (index >= character_sets_by_id_count) {
		return B_BAD_VALUE;
	}	
	*charset = *character_sets_by_id[index++];
	return B_NO_ERROR;
}

status_t
BCharacterSetRoster::RewindCharacterSets()
{
	index = 0;
	if (index >= character_sets_by_id_count) {
		return B_BAD_VALUE;
	}
	return B_NO_ERROR;
}

status_t
BCharacterSetRoster::StartWatching(BMessenger target)
{
	// TODO: implement it
	return B_ERROR;
}

status_t
BCharacterSetRoster::StopWatching(BMessenger target)
{
	// TODO: implement it
	return B_ERROR;
}

const BCharacterSet * 
BCharacterSetRoster::GetCharacterSetByFontID(uint32 id)
{
	return character_sets_by_id[id];
}

const BCharacterSet * 
BCharacterSetRoster::GetCharacterSetByConversionID(uint32 id)
{
	return character_sets_by_id[id+1];
}

const BCharacterSet * 
BCharacterSetRoster::GetCharacterSetByMIBenum(uint32 MIBenum)
{
	return character_sets_by_MIBenum[MIBenum];
}

const BCharacterSet * 
BCharacterSetRoster::FindCharacterSetByPrintName(char * name)
{
	for (int id = 0 ; (id < character_sets_by_id_count) ; id++) {
		if (strcmp(character_sets_by_id[id]->GetPrintName(),name) == 0) {
			return character_sets_by_id[id];
		}
	}
	return 0;
}

const BCharacterSet * 
BCharacterSetRoster::FindCharacterSetByName(char * name)
{
	for (int id = 0 ; (id < character_sets_by_id_count) ; id++) {
		if (strcmp(character_sets_by_id[id]->GetName(),name) == 0) {
			return character_sets_by_id[id];
		}
		const char * mime = character_sets_by_id[id]->GetMIMEName();
		if ((mime != NULL) && (strcmp(mime,name) == 0)) {
			return character_sets_by_id[id];
		}
		for (int alias = 0 ; (alias < character_sets_by_id[id]->CountAliases()) ; alias++) {
			if (strcmp(character_sets_by_id[id]->AliasAt(alias),name) == 0) {
				return character_sets_by_id[id];
			}
		}
	}
	return 0;
}

}
