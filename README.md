# Instructions

- Open an existing remote and expose the PCB
- Wire in the ESP8266 GPIO pins to an existing remote pcb bads

''''
int relayLight = 5; //D1

int relayfanOff = 4; //D2

int relayfanHi = 13; //D7

int relayfanMed = 14; //D5

int relayfanLow = 12 ; //D6  

''''
- Power up ESP8266 after flashing, it should go into setup mode and spin up an AP named "AlexaCntrl_XXX" for you to connect to http://10.0.0.1 and complete setup.
