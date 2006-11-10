// LiveNodeIO.h
// * PURPOSE
//   Manage the import and export of an 'existing node'
//   descriptor -- which refers to a system node
//   (video input, audio mixer, etc.,) a dormant node
//   described in the same document, or some other external
//   node.
//
//   In the first two cases, the node is described by a key
//   string; the following preset key strings correspond to
//   system nodes:
//
//   AUDIO_INPUT
//   AUDIO_OUTPUT
//   AUDIO_MIXER
//   VIDEO_INPUT
//   VIDEO_OUTPUT
//   DAC_TIME_SOURCE
//   SYSTEM_TIME_SOURCE
//
//   There's no way to describe a particular live node instance
//   in a persistent fashion (an instance of the same node created
//   in the future will have a different node ID.)  At the moment,
//   LiveNodeIO describes such nodes via two pieces of information:
//   - the node name
//   - the node's 'kind' bitmask
//
// * HISTORY
//   e.moon		20dec99		begun

#ifndef __LiveNodeIO_H__
#define __LiveNodeIO_H__

#include "NodeRef.h"
#include "XML.h"

#include <String.h>
#include <Entry.h>

#include "cortex_defs.h"
__BEGIN_CORTEX_NAMESPACE

class NodeManager;
class NodeSetIOContext;

class LiveNodeIO :
	public		IPersistent {

public:											// *** ctor/dtor
	virtual ~LiveNodeIO();
	
	LiveNodeIO();
	LiveNodeIO(
		const NodeManager*			manager,
		const NodeSetIOContext*	context,
		media_node_id						node);

	bool exportValid() const { return m_exportValid; }

	// if true, call key() to fetch the key string; otherwise,
	// call name()/kind()
	bool hasKey() const { return m_key.Length() > 0; }

	const char* key() const { return m_key.String(); }
	
	const char* name() const { return m_name.String(); }
	int64 kind() const { return m_kind; }
	
public:											// *** import operations

	// locate the referenced live node
	status_t getNode(
		const NodeManager*			manager,
		const NodeSetIOContext*	context,
		media_node_id*					outNode) const;

public:											// *** document-type setup
	static void AddTo(
		XML::DocumentType*			docType);

public:											// *** IPersistent

	// EXPORT:
	
	void xmlExportBegin(
		ExportContext&					context) const;
		
	void xmlExportAttributes(
		ExportContext&					context) const;
	
	void xmlExportContent(
		ExportContext&					context) const;
	
	void xmlExportEnd(
		ExportContext&					context) const;

	// IMPORT:

	void xmlImportBegin(
		ImportContext&					context);

	void xmlImportAttribute(
		const char*							key,
		const char*							value,
		ImportContext&					context);
	
	void xmlImportContent(
		const char*							data,
		uint32									length,
		ImportContext&					context);
	
	void xmlImportChild(
		IPersistent*						child,
		ImportContext&					context);
			
	void xmlImportComplete(
		ImportContext&					context);
		
private:										// *** implementation

	BString										m_key;
	BString										m_name;
	int64											m_kind;
	
	bool											m_exportValid;
}; 

__END_CORTEX_NAMESPACE
#endif /*__LiveNodeIO_H__*/

