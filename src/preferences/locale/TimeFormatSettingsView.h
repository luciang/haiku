/*
 * Copyright 2009, Adrien Destugues, pulkomandy@gmail.com. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef __TIMEFORMATSETTINGS_H__
#define __TIMEFORMATSETTINGS_H__


#include <Box.h>
#include <String.h>
#include <View.h>


class BCountry;
class BMenuField;
class BMessage;
class BRadioButton;
class BStringView;
class BTextControl;


enum FormatSeparator {
		kNoSeparator,
		kSpaceSeparator,
		kMinusSeparator,
		kSlashSeparator,
		kBackslashSeparator,
		kDotSeparator,
		kSeparatorsEnd
};

const uint32 kSettingsContentsModified = 'Scmo';
const uint32 kMenuMessage = 'FRMT';


class FormatView : public BView {
public:
							FormatView(BCountry* country);

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			AttachedToWindow();

	virtual	void			SetDefaults();
	virtual	bool			IsDefaultable() const;
	virtual	void			Revert();
	virtual	void			SetCountry(BCountry* country);
	virtual	void			RecordRevertSettings();
	virtual	bool			IsRevertable() const;

private:
			void			_UpdateExamples();
			void			_SendNotices();
			void			_ParseDateFormat();
			void			_UpdateLongDateFormatString();

			BRadioButton*	f24HrRadioButton;
			BRadioButton*	f12HrRadioButton;

			BMenuField*		fLongDateMenu[4];
			BString			fLongDateString[4];
			BTextControl*	fLongDateSeparator[4];
			BMenuField*		fDateMenu[3];
			BString			fDateString[3];

			BMenuField*		fSeparatorMenuField;

			BStringView*	fLongDateExampleView;
			BStringView*	fShortDateExampleView;
			BStringView*	fLongTimeExampleView;
			BStringView*	fShortTimeExampleView;
			BStringView*	fNumberFormatExampleView;

			bool			f24HrClock;

			FormatSeparator	fSeparator;
			BString			fDateFormat;

			BCountry*		fCountry;

			BBox*			fDateBox;
			BBox*			fTimeBox;
			BBox*			fNumbersBox;
			BBox*			fCurrencyBox;
};


#endif

