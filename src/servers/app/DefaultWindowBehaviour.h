#ifndef DEFAULT_WINDOW_BEHAVIOUR_H
#define DEFAULT_WINDOW_BEHAVIOUR_H


#include "WindowBehaviour.h"

#include "Decorator.h"


class Desktop;
class Window;


class DefaultWindowBehaviour : public WindowBehaviour
{
	public:
						DefaultWindowBehaviour(Window* window,
							Decorator* decorator);
		virtual			~DefaultWindowBehaviour();

		virtual bool	MouseDown(BMessage* message, BPoint where);
		virtual void	MouseUp(BMessage* message, BPoint where);
		virtual void	MouseMoved(BMessage *message, BPoint where,
							bool isFake);
		
	protected:
		Window*			fWindow;
		Decorator*		fDecorator;
		Desktop*		fDesktop;

		bool			fIsClosing : 1;
		bool			fIsMinimizing : 1;
		bool			fIsZooming : 1;
		bool			fIsSlidingTab : 1;
		bool			fActivateOnMouseUp : 1;

		BPoint			fLastMousePosition;
		float			fMouseMoveDistance;
		bigtime_t		fLastMoveTime;
		bigtime_t		fLastSnapTime;

	private:
		int32			_ExtractButtons(const BMessage* message) const;
		int32			_ExtractModifiers(const BMessage* message) const;
		click_type		_ActionFor(const BMessage* message) const;
		click_type		_ActionFor(const BMessage* message, int32 buttons,
							int32 modifiers) const;

		void			_AlterDeltaForSnap(BPoint& delta,
							bigtime_t now);
};


#endif
