#ifndef GLOBAL_DEFINES_HPP
#define GLOBAL_DEFINES_HPP


typedef unsigned long ulong;


// For serial communication. 115200 Works fine, but probably use 57600 for bluetooth dongle for stability.
#define DEFAULT_BAUD_RATE 115200
#define SERIAL_TIMEOUT_MSECS 1000

#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1284__) \
	|| defined(__AVR_ATmega1284P__) || defined(__AVR_ATmega644__) || defined(__AVR_ATmega644A__)\
	|| defined(__AVR_ATmega644P__) || defined(__AVR_ATmega644PA__)
#define COMPORT Serial2
#else
#define COMPORT Serial
#endif


#endif // GLOBAL_DEFINES_HPP
