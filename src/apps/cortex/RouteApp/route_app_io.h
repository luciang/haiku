// route_app_io.h
// * PURPOSE
//   Central definitions of constants used to import/export
//   XML-formatted data in Cortex.
//
// * HISTORY
//   e.moon		8dec99		Begun

#ifndef __route_app_io_h__
#define __route_app_io_h__

#include "ImportContext.h"
#include "ExportContext.h"
#include "XML.h"

#include <MediaDefs.h>
#include <String.h>

#include "cortex_defs.h"
__BEGIN_CORTEX_NAMESPACE

class NodeManager;
class NodeSetIOContext;

// IPersistent elements
extern const char* const _DORMANT_NODE_ELEMENT;
extern const char* const _LIVE_NODE_ELEMENT;
extern const char* const _CONNECTION_ELEMENT;

extern const char* const _NODE_GROUP_ELEMENT;

// simple string-content elements
extern const char* const _NAME_ELEMENT;
extern const char* const _FLAG_ELEMENT;
extern const char* const _KIND_ELEMENT;
extern const char* const _FLAVOR_ID_ELEMENT;
extern const char* const _CYCLE_ELEMENT;
extern const char* const _RUN_MODE_ELEMENT;
extern const char* const _TIME_SOURCE_ELEMENT;
extern const char* const _RECORDING_DELAY_ELEMENT;
extern const char* const _REF_ELEMENT;

// intermediate elements
extern const char* const _OUTPUT_ELEMENT;
extern const char* const _INPUT_ELEMENT;
extern const char* const _NODE_SET_ELEMENT;
extern const char* const _UI_STATE_ELEMENT;

// system-defined node keys
extern const char* const _AUDIO_INPUT_KEY;
extern const char* const _AUDIO_OUTPUT_KEY;
extern const char* const _AUDIO_MIXER_KEY;
extern const char* const _VIDEO_INPUT_KEY;
extern const char* const _VIDEO_OUTPUT_KEY;

// helper functions

void _write_simple(
	const char* element,
	const char* value,
	ExportContext& context);

void _write_node_kinds(
	int64 kinds,
	ExportContext& context);

void _read_node_kind(
	int64& ioKind,
	const char* data,
	ImportContext& context);
	
// fills in either key or outName/kind for the provided
// node.  If the given node is one of the default system nodes,
// an appropriate 'preset' key value will be returned.

status_t _get_node_signature(
	const NodeManager*			manager,
	const NodeSetIOContext*	context,
	media_node_id						node,
	BString&								outKey,
	BString&								outName,
	int64&									outKind);
	
// given a name and kind, looks for a matching node

status_t _match_node_signature(
	const char*							name,
	int64										kind,
	media_node_id*					outNode);

// given a key, looks for a system-default node

status_t _match_system_node_key(
	const char*							key,
	const NodeManager*			manager,
	media_node_id*					outNode);

// adds mappings for the simple string-content elements to the
// given document type

void _add_string_elements(
	XML::DocumentType*			docType);

__END_CORTEX_NAMESPACE
#endif /*__route_app_io_h__*/

