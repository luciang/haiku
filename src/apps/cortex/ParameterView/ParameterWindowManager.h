// ParameterWindowManager.h
//
// * PURPOSE
//	 Manages all the ParameterWindows and control panels.
//   Will not let you open multiple windows referring to
//	 the same node, and takes care of quitting them all
//	 when shut down.
//
// * HISTORY
//   c.lenz		17feb2000		Begun
//

#ifndef __ParameterWindowManager_H__
#define __ParameterWindowManager_H__

#include <Looper.h>
#include <Point.h>

class BList;
class BWindow;

#include "cortex_defs.h"
__BEGIN_CORTEX_NAMESPACE

class NodeRef;

class ParameterWindowManager :
	public	BLooper {
	
public:								// *** constants

	// the screen position where the first window should
	// be displayed
	static const BPoint				M_INIT_POSITION;

	// horizontal/vertical offset by which subsequent
	// parameter windows positions are shifted
	static const BPoint 			M_DEFAULT_OFFSET;

private:							// *** ctor/dtor

	// hidden ctor; is called only from inside Instance()
									ParameterWindowManager();

public:

	// quits all registered parameter windows and control
	// panels
	virtual							~ParameterWindowManager();

public:								// *** singleton access

	// access to the one and only instance of this class
	static ParameterWindowManager  *Instance();

	// will delete the singleton instance and take down all
	// open windows
	static void						shutDown();

public:								// *** operations

	// request a ParameterWindow to be openened for the given
	// NodeRef; registeres the window
	status_t						openWindowFor(
										const NodeRef *ref);

	// request to call StartControlPanel() for the given
	// NodeRef; registeres the panels messenger
	status_t						startControlPanelFor(
										const NodeRef *ref);

public:								// *** BLooper impl

	virtual void					MessageReceived(
										BMessage *message);

private:							// *** internal operations

	// ParameterWindow management
	bool							_addWindowFor(
										const NodeRef *ref,
										BWindow *window);
	bool							_findWindowFor(
										int32 nodeID,
										BWindow **outWindow);
	void							_removeWindowFor(
										int32 nodeID);

	// ControlPanel management
	bool							_addPanelFor(
										int32 id,
										const BMessenger &messenger);
	bool							_findPanelFor(
										int32 nodeID,
										BMessenger *outMessenger);
	void							_removePanelFor(
										int32 nodeID);

private:							// *** data members

	// a list of window_map_entry objects, ie parameter windows
	// identified by the node_id
	BList						   *m_windowList;

	// a list of window_map_entry objects, this time for
	// node control panels
	BList						   *m_panelList;
	
	// the BPoint at which the last InfoWindow was initially
	// opened
	BPoint							m_lastWindowPosition;

private:							// *** static members

	// the magic singleton instance
	static ParameterWindowManager  *s_instance;
};

__END_CORTEX_NAMESPACE
#endif /*__ParameterWindowManager_H__*/
