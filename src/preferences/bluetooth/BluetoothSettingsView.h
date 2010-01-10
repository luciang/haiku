/*
 * Copyright 2008-09, Oliver Ruiz Dorantes, <oliver.ruiz.dorantes_at_gmail.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ANTIALIASING_SETTINGS_VIEW_H
#define ANTIALIASING_SETTINGS_VIEW_H


#include <View.h>

class BBox;
class BMenuField;
class BPopUpMenu;
class BSlider;

class ExtendedLocalDeviceView;

class BluetoothSettingsView : public BView {
public:
							BluetoothSettingsView(const char* name);
	virtual					~BluetoothSettingsView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);


private:
			void			_BuildConnectionPolicy();
			void			_BuildClassMenu();
			void			_BuildLocalDevicesMenu();

protected:
			float			fDivider;

			BMenuField*		fPolicyMenuField;
			BPopUpMenu*		fPolicyMenu;
			BMenuField*		fClassMenuField;
			BPopUpMenu*		fClassMenu;
			BMenuField*		fLocalDevicesMenuField;
			BPopUpMenu*		fLocalDevicesMenu;

			ExtendedLocalDeviceView* fExtDeviceView;

			BSlider*		fInquiryTimeControl;

};

#endif // ANTIALIASING_SETTINGS_VIEW_H
