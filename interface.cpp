//
// Title        : UNO2IEC - interface implementation, arduino side.
// Author       : Lars Wadefalk
// Version      : 0.1
// Target MCU   : Arduino Uno AtMega328(H, 5V) at 16 MHz, 2KB SRAM, 32KB flash, 1KB EEPROM.
//
// CREDITS:
// --------
// The UNO2IEC application is inspired by Lars Pontoppidan's MMC2IEC project.
// It has been ported to C++.
// The MMC2IEC application is inspired from Jan Derogee's 1541-III project for
// PIC: http://jderogee.tripod.com/
// This code is a complete reimplementation though, which includes some new
// features and excludes others.
//
// DESCRIPTION:
// This "interface" class is the main driving logic for the IEC command handling.
//
// Commands from the IEC communication are interpreted, and the appropriate data
// from either Native, D64, T64, M2I, x00 image formats is sent back.
//
// DISCLAIMER:
// The author is in no way responsible for any problems or damage caused by
// using this code. Use at your own risk.
//
// LICENSE:
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//

#include <string.h>
#include "global_defines.h"
#include "interface.h"





namespace {

// Buffer for incoming and outgoing serial bytes and other stuff.
char serCmdIOBuf[256];



} // unnamed namespace


Interface::Interface(IEC& iec)
	: m_iec(iec)

	// NOTE: Householding with RAM bytes: We use the middle of serial buffer for the ATNCmd buffer info.
	// This is ok and won't be overwritten by actual serial data from the host, this is because when this ATNCmd data is in use
	// only a few bytes of the actual serial data will be used in the buffer.
	, m_cmd(*reinterpret_cast<IEC::ATNCmd*>(&serCmdIOBuf[sizeof(serCmdIOBuf) / 2]))
{


} // ctor




void Interface::sendStatus(void)
{
	byte i, readResult;
 
	COMPORT.write('E'); // ask for error string from the last queued error.
	COMPORT.write(m_queuedError);

	// first sync the response.
	do {
		readResult = COMPORT.readBytes(serCmdIOBuf, 1);
	} while(readResult not_eq 1 or serCmdIOBuf[0] not_eq ':');
	// get the string.
	readResult = COMPORT.readBytesUntil('\r', serCmdIOBuf, sizeof(serCmdIOBuf));
	if(not readResult)
		return; // something went wrong with result from host.

	// Length does not include the CR, write all but the last one should be with EOI.
	for(i = 0; i < readResult - 2; ++i)
		m_iec.send(serCmdIOBuf[i]);
	// ...and last byte in string as with EOI marker.
	m_iec.sendEOI(serCmdIOBuf[i]);
} // sendStatus


// send single basic line, including heading basic pointer and terminating zero.
void Interface::sendLine(byte len, char* text, word& basicPtr)
{
	byte i;

	// Increment next line pointer
	// note: minus two here because the line number is included in the array already.
	basicPtr += len + 5 - 2;

	// Send that pointer
	m_iec.send(basicPtr bitand 0xFF);
	m_iec.send(basicPtr >> 8);

	// Send line number
//	m_iec.send(lineNo bitand 0xFF);
//	m_iec.send(lineNo >> 8);

	// Send line contents
	for(i = 0; i < len; i++)
		m_iec.send(text[i]);

	// Finish line
	m_iec.send(0);
} // sendLine


void Interface::sendListing()
{
	// Reset basic memory pointer:
	word basicPtr = 0x0101;//dir startadress
	noInterrupts();
	// Send load address
	m_iec.send(basicPtr bitand 0xff);
	m_iec.send((basicPtr >> 8) bitand 0xff);
	interrupts();
	// This will be slightly tricker: Need to specify the line sending protocol between Host and Arduino.
	// Call the listing function
	byte resp;
	do {
		COMPORT.write('L'); // initiate request.
		COMPORT.readBytes(serCmdIOBuf, 2);
		resp = serCmdIOBuf[0];
		if('L' == resp) { // Host system will give us something else if we're at last line to send.
			// get the length as one byte: This is kind of specific: For listings we allow 256 bytes length. Period.
			byte len = serCmdIOBuf[1];
			// WARNING: Here we might need to read out the data in portions. The serCmdIOBuf might just be too small
			// for very long lines.
			byte actual = COMPORT.readBytes(serCmdIOBuf, len);
			if(len == actual) {
				// send the bytes directly to CBM!
				noInterrupts();
				sendLine(len, serCmdIOBuf, basicPtr);
				interrupts();
			}
			else {
				resp = 'E'; // just to end the pain. We're out of sync or somthin'
				sprintf_P(serCmdIOBuf, (PGM_P)F("Expected: %d chars, got %d."), len, actual);
				//Log(Error, FAC_IFACE, serCmdIOBuf);
			}
		}
		else {
			if('l' not_eq resp) {
				sprintf_P(serCmdIOBuf, (PGM_P)F("Ending at char: %d."), resp);
				//log(Error, FAC_IFACE, serCmdIOBuf);
				COMPORT.readBytes(serCmdIOBuf, sizeof(serCmdIOBuf));
				//log(Error, FAC_IFACE, serCmdIOBuf);
			}
		}
	} while('L' == resp); // keep looping for more lines as long as we got an 'L' indicating we haven't reached end.

	// End program with two zeros after last line. Last zero goes out as EOI.
	noInterrupts();
	m_iec.send(0);
	m_iec.sendEOI(0);
	interrupts();
} // sendListing


void Interface::sendFile()
{
	// Send file bytes, such that the last one is sent with EOI.
	byte resp;
	COMPORT.write('S'); // ask for file size.
	byte len = COMPORT.readBytes(serCmdIOBuf, 3);
	// it is supposed to answer with S<highByte><LowByte>
	if(3 not_eq len or serCmdIOBuf[0] not_eq 'S')
		return; // got some garbage response.
	word bytesDone = 0, totalSize = (((word)((byte)serCmdIOBuf[1])) << 8) bitor (byte)(serCmdIOBuf[2]);


	bool success = true;
	// Initial request for a bunch of bytes, here we specify the read size for every subsequent 'R' command.
	// This begins the transfer "game".
	COMPORT.write('N');											// ask for a byte/bunch of bytes
	COMPORT.write(256);		// specify the arduino serial library buffer limit for best performance / throughput.
	do {
		len = COMPORT.readBytes(serCmdIOBuf, 2); // read the ack type ('B' or 'E')
		if(2 not_eq len) {
			strcpy_P(serCmdIOBuf, (PGM_P)F("2 Host bytes expected, stopping"));
			//log(Error, FAC_IFACE, serCmdIOBuf);
			success = false;
			break;
		}
		resp = serCmdIOBuf[0];
		len = serCmdIOBuf[1];
		if('B' == resp or 'E' == resp) {
			byte actual = COMPORT.readBytes(serCmdIOBuf, len);
			if(actual not_eq len) {
				strcpy_P(serCmdIOBuf, (PGM_P)F("Host bytes expected, stopping"));
				success = false;
				//log(Error, FAC_IFACE, serCmdIOBuf);
				break;
			}

			// so we get some bytes, send them to CBM.
			for(byte i = 0; success and i < len; ++i) { // End if sending to CBM fails.

				if(resp == 'E' and i == len - 1)
					success = m_iec.sendEOI(serCmdIOBuf[i]); // indicate end of file.
				else
					success = m_iec.send(serCmdIOBuf[i]);

				++bytesDone;


			}

		}
		else {
			strcpy_P(serCmdIOBuf, (PGM_P)F("Got unexp. cmd resp.char."));
			//log(Error, FAC_IFACE, serCmdIOBuf);
			success = false;
		}
	} while(resp == 'B' and success); // keep asking for more as long as we don't get the 'E' or something else (indicating out of sync).
	// If something failed and we have serial bytes in recieve queue we need to flush it out.
	if(not success and COMPORT.available()) {
		while(COMPORT.available())
			COMPORT.read();
	}

	if(success) {
		sprintf_P(serCmdIOBuf, (PGM_P)F("Transferred %u of %u bytes."), bytesDone, totalSize);
		//log(Success, FAC_IFACE, serCmdIOBuf);
	}
} // sendFile


void Interface::saveFile()
{
	boolean done = false;
	// Recieve bytes until a EOI is detected
	serCmdIOBuf[0] = 'W';
	do {
		byte bytesInBuffer = 2;
		do {
			noInterrupts();
			serCmdIOBuf[bytesInBuffer++] = m_iec.receive();
			interrupts();
			done = (m_iec.state() bitand IEC::eoiFlag) or (m_iec.state() bitand IEC::errorFlag);
		} while((bytesInBuffer < 0xf0) and not done);
		// indicate to media host that we want to write a buffer. Give the total length including the heading 'W'+length bytes.
		serCmdIOBuf[1] = bytesInBuffer;
		COMPORT.write((const byte*)serCmdIOBuf, bytesInBuffer);
		COMPORT.flush();
	} while(not done);
} // saveFile


byte Interface::handler(void)
{
	noInterrupts();
	IEC::ATNCheck retATN = m_iec.checkATN(m_cmd);
	interrupts();

	if(retATN == IEC::ATN_ERROR) {
		strcpy_P(serCmdIOBuf, (PGM_P)F("ATNCMD: IEC_ERROR!"));
		//log(Error, FAC_IFACE, serCmdIOBuf);
		
	}
	// Did anything happen from the host side?
	else if(retATN not_eq IEC::ATN_IDLE) {
		// A command is recieved, make cmd string null terminated
		m_cmd.str[m_cmd.strLen] = '\0';

		{
			COMPORT.write("@ ");
			//log(Information, FAC_IFACE, serCmdIOBuf);
		}


		// lower nibble is the channel.
		byte chan = m_cmd.code bitand 0x0F;

		// check upper nibble, the command itself.
		switch(m_cmd.code bitand 0xF0) {
			case IEC::ATN_CODE_OPEN:
				// Open either file or prg for reading, writing or single line command on the command channel.
				// In any case we just issue an 'OPEN' to the host and let it process.
				// Note: Some of the host response handling is done LATER, since we will get a TALK or LISTEN after this.
				// Also, simply issuing the request to the host and not waiting for any response here makes us more
				// responsive to the CBM here, when the DATA with TALK or LISTEN comes in the next sequence.
				handleATNCmdCodeOpen(m_cmd);
			break;

			case IEC::ATN_CODE_DATA:  // data channel opened
				if(retATN == IEC::ATN_CMD_TALK) {
        COMPORT.write("TALK ");
					 // when the CMD channel is read (status), we first need to issue the host request. The data channel is opened directly.
					if(15 == chan)
						handleATNCmdCodeOpen(m_cmd); COMPORT.write(" This is typically an empty command");// This is typically an empty command,







            
					handleATNCmdCodeDataTalk(chan); // ...but we do expect a response from PC that we can send back to CBM.
         
				}
				else if(retATN == IEC::ATN_CMD_LISTEN){
        COMPORT.write(" LISTEN ");
					handleATNCmdCodeDataListen();}
				else if(retATN == IEC::ATN_CMD) // Here we are sending a command to PC and executing it, but not sending response
					handleATNCmdCodeOpen(m_cmd);	// back to CBM, the result code of the command is however buffered on the PC side.
				break;

			case IEC::ATN_CODE_CLOSE:
				// handle close with host.
				handleATNCmdClose();
				break;

			case IEC::ATN_CODE_LISTEN:
				//log(Information, FAC_IFACE, "LISTEN");
				break;
			case IEC::ATN_CODE_TALK:
				//log(Information, FAC_IFACE, "TALK");
				break;
			case IEC::ATN_CODE_UNLISTEN:
				//log(Information, FAC_IFACE, "UNLISTEN");
				break;
			case IEC::ATN_CODE_UNTALK:
				//Log(Information, FAC_IFACE, "UNTALK");
				break;
		} // switch
	} // IEC not idle

	return retATN;
} // handler










void Interface::handleATNCmdCodeOpen(IEC::ATNCmd& cmd)
{
	serCmdIOBuf[0] = 'i';
	serCmdIOBuf[2] = cmd.code bitand 0xF;
	byte length = 3;
	memcpy(&serCmdIOBuf[length], cmd.str, cmd.strLen);
	length += cmd.strLen;
	// Set the length so that receiving side know how much to read out.
	serCmdIOBuf[1] = length;
	// NOTE: Host side handles BOTH file open command AND the command channel command (from the cmd.code).
	COMPORT.write((const byte*)serCmdIOBuf, length);
} // handleATNCmdCodeOpen


void Interface::handleATNCmdCodeDataTalk(byte chan)
{
	byte lengthOrResult;
	boolean wasSuccess = false;

	// process response into m_queuedError.
	// Response: ><code in binary><CR>

	serCmdIOBuf[0] = 0;
	do {
		lengthOrResult = '>';
	} while(lengthOrResult not_eq 1 or serCmdIOBuf[0] not_eq '>');

	if(not lengthOrResult or '>' not_eq serCmdIOBuf[0]) {
		m_iec.sendFNF();
		strcpy_P(serCmdIOBuf, (PGM_P)F("response not sync."));
		//log(Error, FAC_IFACE, serCmdIOBuf);
	}
	else {
		if(true) {
			if(true) {
				lengthOrResult = serCmdIOBuf[0];
				wasSuccess = true;
			}
			//else
				//log(Error, FAC_IFACE, serCmdIOBuf);
		}
		if(15 == chan) {
			m_queuedError = wasSuccess ? lengthOrResult : 97;
			// Send status message
			sendStatus();
			// go back to OK state, we have dispatched the error to IEC host now.
			m_queuedError = 0;
		}
		else {
			m_openState = wasSuccess ? lengthOrResult : O_NOTHING;

			switch(m_openState) {
			case O_INFO:
				// Reset and send SD card info
			
				sendListing();
				break;

			case O_FILE_ERR:
				// FIXME: interface with Host for error info.
				//sendListing(/*&send_file_err*/);
				m_iec.sendFNF();
				break;

			case O_NOTHING:
				// Say file not found
				m_iec.sendFNF();
				break;

			case O_FILE:
				// Send program file
				sendFile();
				break;

			case O_DIR:
				// Send listing
				sendListing();
				break;
			}
		}
	}
//	//log(Information, FAC_IFACE, serCmdIOBuf);
} // handleATNCmdCodeDataTalk


void Interface::handleATNCmdCodeDataListen()
{
	byte lengthOrResult;
	boolean wasSuccess = false;

	// process response into m_queuedError.
	// Response: ><code in binary><CR>

	serCmdIOBuf[0] = 0;
	do {
		lengthOrResult = COMPORT.readBytes(serCmdIOBuf, 1);
	} while(lengthOrResult not_eq 1 or serCmdIOBuf[0] not_eq '>');

	if(not lengthOrResult or '>' not_eq serCmdIOBuf[0]) {
		// FIXME: Check what the drive does here when things go wrong. FNF is probably not right.
		m_iec.sendFNF();
		strcpy_P(serCmdIOBuf, (PGM_P)F("response not sync."));
		//log(Error, FAC_IFACE, serCmdIOBuf);
	}
	else {
		if(lengthOrResult = COMPORT.readBytes(serCmdIOBuf, 2)) {
			if(2 == lengthOrResult) {
				lengthOrResult = serCmdIOBuf[0];
				wasSuccess = true;
			}
			//else
				//log(Error, FAC_IFACE, serCmdIOBuf);
		}
		m_queuedError = wasSuccess ? lengthOrResult : 97;

		if(0 == m_queuedError)
			saveFile();
//		else // FIXME: Check what the drive does here when saving goes wrong. FNF is probably not right. Dummyread entire buffer from CBM?
//			m_iec.sendFNF();
	}
} // handleATNCmdCodeDataListen


void Interface::handleATNCmdClose()
{
	// handle close of file. Host system will return the name of the last loaded file to us.
	COMPORT.print("C");
	COMPORT.readBytes(serCmdIOBuf, 2);
	byte resp = serCmdIOBuf[0];
	if('N' == resp or 'n' == resp) { // N indicates we have a name. Case determines whether we loaded or saved data.
		// get the length of the name as one byte.
		byte len = serCmdIOBuf[1];
		byte actual = COMPORT.readBytes(serCmdIOBuf, len);
		if(len == actual) {

		}
		else {
			sprintf_P(serCmdIOBuf, (PGM_P)F("Exp: %d chars, got %d."), len, actual);
			//log(Error, FAC_IFACE, serCmdIOBuf);
		}
	}
	else if('C' == resp) {
		if(m_iec.deviceNumber() not_eq serCmdIOBuf[1])
			m_iec.setDeviceNumber(serCmdIOBuf[1]);
	}
} // handleATNCmdClose
