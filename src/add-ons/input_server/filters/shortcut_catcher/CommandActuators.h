/*
 * Copyright 1999-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jeremy Friesner
 */


#ifndef CommandActuators_h
#define CommandActuators_h

#include <Message.h>
#include <String.h>
#include <Archivable.h>
#include <List.h>
#include <InputServerFilter.h>

#ifndef __INTEL__
#pragma export on 
#endif

struct key_map; // declaration

class CommandActuator;

// 	Factory function: Given a text string, allocates and returns a 
//	CommandActuator. Returns NULL on failure (usually a parse error)
extern CommandActuator* CreateCommandActuator(const char* command);

// 	This file contains various CommandActuator classes. Each CommandActuator 
//	contains code to do something. They're functor objects, really. The input 
//	server add-on will execute the CommandActuator associated with a key combo
// 	when that key combo is detected.

// The abstract base class. Defines the interface.
_EXPORT class CommandActuator;
class CommandActuator : public BArchivable {
public:
								CommandActuator(int32 argc, char** argv);
								CommandActuator(BMessage* from);

		// Called by the InputFilter whenever a key is pressed or depressed. 
		// It's important to ensure that this method returns quickly, as the 
		// input_server will block while it executes. (keyMsg) is the BMessage 
		// that triggered this call. (outlist) is a BList that additional input 
		// events may be added to. If (*asyncData) is set to non-NULL, 
		// KeyEventAsync() will be called asynchronously with (asyncData) as 
		// the argument. Returns the filter_result to be given back to the 
		// input_server. (Defaults to B_SKIP_MESSAGE)
		virtual filter_result	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** asyncData, 
									BMessage* lastMouseMove) 
									{return B_SKIP_MESSAGE;}

		// Called in a separate thread if (*setAsyncData) was set to non-NULL 
		// in KeyEvent(). Defaults to a no-op.
		virtual void 			KeyEventAsync(const BMessage* keyUpMsg, 
									void* asyncData) {}
									
		virtual status_t 		Archive(BMessage* into, bool deep = true) 
									const;
};


// This is the most common thing to do--launch a process.
_EXPORT class LaunchCommandActuator;
class LaunchCommandActuator : public CommandActuator {
public:
								LaunchCommandActuator(int32 argc, char** argv);
								LaunchCommandActuator(BMessage* from);
								~LaunchCommandActuator();

		virtual	status_t 		Archive(BMessage* into, bool deep = true) 
									const;
		static	BArchivable*	Instantiate(BMessage* from);
		virtual filter_result 	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);
		virtual void 			KeyEventAsync(const BMessage* keyMsg, 
									void* asyncData);

private:
				bool 			_GetNextWord(char** setBegin, char** setEnd) 
									const;

				char**			fArgv;
				int32			fArgc;
};


// Stupid actuator--just calls beep().
_EXPORT class BeepCommandActuator;
class BeepCommandActuator : public CommandActuator {
public:
								BeepCommandActuator(int32 argc, char** argv);
								BeepCommandActuator(BMessage* from);
								~BeepCommandActuator();

		virtual	filter_result 	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);
		virtual status_t 		Archive(BMessage* into, bool deep = true) 
									const;
		static BArchivable* 	Instantiate(BMessage* from);
};


// This class will insert a string of keystrokes into the input stream.
_EXPORT class KeyStrokeSequenceCommandActuator;
class KeyStrokeSequenceCommandActuator : public CommandActuator {
public:
								KeyStrokeSequenceCommandActuator(int32 argc, 
									char** argv);
								KeyStrokeSequenceCommandActuator(
									BMessage* from);
								~KeyStrokeSequenceCommandActuator();

		virtual filter_result 	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);
		virtual status_t 		Archive(BMessage* into, bool deep = true) 
									const;
		static 	BArchivable* 	Instantiate(BMessage * from);
private:
				void			_GenerateKeyCodes();
				int32 			_LookupKeyCode(key_map* map, char* keys, 
									int32 offsets[128], char key, 
									uint8* setStates, int32& setMod, 
									int32 setTo) const;
				void 			_SetStateBit(uint8* setStates, uint32 key, 
									bool on = true) const;

				uint8* 			fStates;
				int32* 			fKeyCodes;
				int32* 			fModCodes;
				BString 		fSequence;
				BList 			fOverrides;
				BList 			fOverrideOffsets;
				BList 			fOverrideModifiers;
				BList 			fOverrideKeyCodes;
};


// This class will insert a string of keystrokes into the input stream.
_EXPORT class MIMEHandlerCommandActuator;
class MIMEHandlerCommandActuator : public CommandActuator {
public:
								MIMEHandlerCommandActuator(int32 argc, 
									char** argv);
								MIMEHandlerCommandActuator(BMessage* from);
								~MIMEHandlerCommandActuator();

		virtual	filter_result 	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);
		virtual void 			KeyEventAsync(const BMessage* keyUpMsg, 
									void* asyncData);
		virtual status_t 		Archive(BMessage * into, bool deep = true) 
									const;
		static 	BArchivable* 	Instantiate(BMessage* from);

private:
				BString 		fMimeType;
};


// Abstract base class for actuators that affect mouse buttons
_EXPORT class MouseCommandActuator;
class MouseCommandActuator : public CommandActuator {
public:
								MouseCommandActuator(int32 argc, char** argv);
								MouseCommandActuator(BMessage* from);
								~MouseCommandActuator();

		virtual	status_t		Archive(BMessage* into, bool deep = true) 
									const;

protected:
				int32			_GetWhichButtons() const;
				void 			_GenerateMouseButtonEvent(bool mouseDown, 
									const BMessage* keyMsg, BList* outlist, 
									BMessage* lastMouseMove);

private:
				int32			fWhichButtons;
};


// This class sends a single mouse down event when activated, causing the mouse
// pointer to enter a "sticky down" state. Good for some things(like dragging).
_EXPORT class MouseDownCommandActuator;
class MouseDownCommandActuator : public MouseCommandActuator {
public:
								MouseDownCommandActuator(int32 argc, 
									char** argv);
								MouseDownCommandActuator(BMessage* from);
								~MouseDownCommandActuator();

		virtual	filter_result 	KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);
		virtual	status_t 		Archive(BMessage* into, bool deep = true) 
									const;
		static	BArchivable * 	Instantiate(BMessage * from);
};


// This class sends a single mouse down up when activated, releasing any 
// previously set "sticky down" state. Good for some things (like dragging).
_EXPORT class MouseUpCommandActuator;
class MouseUpCommandActuator : public MouseCommandActuator {
public:
								MouseUpCommandActuator(int32 argc, 
									char** argv);
								MouseUpCommandActuator(BMessage * from);
								~MouseUpCommandActuator();

	virtual filter_result 		KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);

	virtual status_t 			Archive(BMessage* into, bool deep = true)
									const;
	static 	BArchivable* 		Instantiate(BMessage* from);
};


// This class will send B_MOUSE_UP and B_MOUSE_DOWN events whenever B_KEY_UP or
// B_KEY_DOWN events are detected for its key This way a key can act sort of 
// like a mouse button.
_EXPORT class MouseButtonCommandActuator;
class MouseButtonCommandActuator : public MouseCommandActuator {
public:
								MouseButtonCommandActuator(int32 argc, 
									char** argv);
								MouseButtonCommandActuator(BMessage* from);
								~MouseButtonCommandActuator();

	virtual filter_result 		KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** setAsyncData, 
									BMessage* lastMouseMove);

	virtual status_t 			Archive(BMessage* into, bool deep = true)
									const;
	static 	BArchivable* 		Instantiate(BMessage* from);

private:
			bool 				fKeyDown ;
};


// Base class for some actuators that control the position of the mouse pointer
_EXPORT class MoveMouseCommandActuator;
class MoveMouseCommandActuator : public CommandActuator {
public: 
								MoveMouseCommandActuator(int32 argc,
									char** argv);
								MoveMouseCommandActuator(BMessage* from);
								~MoveMouseCommandActuator();

	virtual	status_t			Archive(BMessage* into, bool deep) const;

protected:
			void 				CalculateCoords(float& setX, float& setY) 
									const;
			BMessage* 			CreateMouseMovedMessage(const BMessage* origMsg
									, BPoint p, BList* outlist) const;

private:
			void 				_ParseArg(const char* arg, float& setPercent, 
									float& setPixels) const;

			float 				fXPercent;
			float 				fYPercent;
			float 				fXPixels;
			float 				fYPixels;
};


// Actuator that specifies multiple sub-actuators to be executed in series
_EXPORT class MultiCommandActuator;
class MultiCommandActuator : public CommandActuator {
public: 
								MultiCommandActuator(int32 argc, char** argv);
								MultiCommandActuator(BMessage* from);
								~MultiCommandActuator();

	virtual status_t			Archive(BMessage* into, bool deep) const;
	virtual filter_result		KeyEvent(const BMessage* keyMsg, 
									BList* outlist, void** asyncData, 
									BMessage* lastMouseMove);
	virtual void				KeyEventAsync(const BMessage* keyUpMsg, 
									void * asyncData);
	static	BArchivable*		Instantiate(BMessage* from);

private:
			BList				fSubActuators;
};


// Actuator for moving a mouse relative to its current position
_EXPORT class MoveMouseToCommandActuator;
class MoveMouseToCommandActuator : public MoveMouseCommandActuator {
public:
								MoveMouseToCommandActuator(int32 argc, 
									char** argv);
								MoveMouseToCommandActuator(BMessage* from);
								~MoveMouseToCommandActuator();

	virtual	filter_result		KeyEvent(const BMessage* keyMsg, BList* outlist
									, void** setAsyncData, 
									BMessage* lastMouseMove);

	virtual status_t			Archive(BMessage* into, bool deep = true) 
									const;
	static BArchivable*			Instantiate(BMessage* from);
};


// Actuator for moving a mouse relative to its current position
_EXPORT class MoveMouseByCommandActuator;
class MoveMouseByCommandActuator : public MoveMouseCommandActuator {
public:
									MoveMouseByCommandActuator(int32 argc, 
										char** argv);
									MoveMouseByCommandActuator(BMessage* from);
									~MoveMouseByCommandActuator();

	virtual filter_result 			KeyEvent(const BMessage* keyMsg, 
										BList* outlist, void** setAsyncData, 
										BMessage* lastMouseMove); 
	virtual status_t 				Archive(BMessage * into, bool deep = true) 
										const;
	static 	BArchivable*			Instantiate(BMessage * from);
};


// Actuator to send BMessage to an application - written by Daniel Wesslen
_EXPORT class SendMessageCommandActuator;
class SendMessageCommandActuator : public CommandActuator {
public:
									SendMessageCommandActuator(int32 argc, 
										char** argv);
									SendMessageCommandActuator(BMessage* from);
									~SendMessageCommandActuator();

	virtual filter_result			KeyEvent(const BMessage* keyMsg, 
										BList* outlist, void** setAsyncData, 
										BMessage* lastMouseMove);
	virtual void					KeyEventAsync(const BMessage* keyUpMsg, 
										void* asyncData);
	virtual status_t				Archive(BMessage* into, bool deep = true) 
										const;
	static 	BArchivable*			Instantiate(BMessage * from);

private:
			void					_ParseFloatArgs(float* outArgs, int maxArgs
										, const char* str) const;

			BString					fSignature;
			BMessage				fSendMsg;
};

#ifndef __INTEL__
#pragma export reset
#endif

#endif
