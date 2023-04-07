#ifndef INTERFACE_H
#define INTERFACE_H

#include "iec_driver.h"




enum OpenState {
	O_NOTHING,			// Nothing to send / File not found error
	O_INFO,					// User issued a reload sd card
	O_FILE,					// A program file is opened
	O_DIR,					// A listing is requested
	O_FILE_ERR,			// Incorrect file format opened
	O_SAVE_REPLACE	// Save-with-replace is requested
};



class Interface
{
public:
	Interface(IEC& iec);
	virtual ~Interface() {}

	// The handler returns the current IEC state, see the iec_driver.hpp for possible states.
	byte handler(void);
	// Keeping the system date and time as set on a specific moment. The millis() will then keep the elapsed time since
	// moment the time was set.
	void setDateTime(word year, byte month, byte day, byte hour, byte minute, byte second);
	// retrieve the date and time as strings. Current time will be updated according to the elapsed millis before formatting.
	// String will be of format "yyyymmdd hhmmss", if timeOnly is true only the time part will be returned as
	// "hhmmss", this fits the TIME$ variable of cbm basic 2.0 and later.
	char* dateTimeString(char* dest, bool timeOnly);



private:
	
	void saveFile();
	void sendFile();
	void sendListing(/*PFUNC_SEND_LISTING sender*/);
	void sendStatus(void);
	bool removeFilePrefix(void);
	void sendLine(byte len, char* text, word &basicPtr);

	// handler helpers.
	void handleATNCmdCodeOpen(IEC::ATNCmd &cmd);
	void handleATNCmdCodeDataTalk(byte chan);
	void handleATNCmdCodeDataListen();
	void handleATNCmdClose();

	void updateDateTime();

	// our iec low level driver:
	IEC& m_iec;
	// This var is set after an open command and determines what to send next
	byte m_openState;			// see OpenState
	byte m_queuedError;

	// time and date and moment of setting.
	
	ulong m_timeOfSet;

	// atn command buffer struct
	IEC::ATNCmd& m_cmd;

};

#endif
