#include "global_defines.h"
#include "iec_driver.h"
#include "interface.h"

// The global IEC handling singleton:
static IEC iec(11);

static Interface iface(iec);


void setup()
{


	// Initialize serial and wait for port to open:
	COMPORT.begin(DEFAULT_BAUD_RATE);
	COMPORT.setTimeout(SERIAL_TIMEOUT_MSECS);
// set all digital pins in a defined state.
iec.init();

} // setup


void loop()
{
IEC::Dumy == iface.handler();
//IEC::ATNCheck;


} // loop
