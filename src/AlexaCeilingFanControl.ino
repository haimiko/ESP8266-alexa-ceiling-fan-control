#include <ESP8266httpUpdate.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include "switch.h"
#include "UpnpBroadcastResponder.h"
#include "CallbackFunction.h"
#include "DNSServer.h"
#include <string>

const String Version = "1.0";
#define MYDEBUG 1

bool SETUPMODE = false;

//Initial Setup AP Password
//const char *password = "1234567890"; //no password for AP
String esid;
String host;
String myhostname;
String epass;
int PORT;
#define AP_PIN 16 //D4-GPIO2 of Node MCU ; factory reset pin
// Set Relay Pins
int relayLight = 5; //D1
int relayfanOff = 4; //D2
int relayfanHi = 13; //D7
int relayfanMed = 14; //D5
int relayfanLow = 12 ; //D6  ; NEVER USE D3 - it's flash!!!

ESP8266WebServer server(80);

//Initialize OTA variables
int OTA_REQ_PORT = 9997;
int OTA_SETUP = 9996;
String OTA_HOST = "OTAHOSTNAME";

boolean OTAMode = false;


//Initialize RTC
/*
extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

#define RTCMEMORYSTART 66
#define RTCMEMORYLEN 120
#define VCCFACTOR 119 //119 for ESP01 and 179 for ESP12


typedef struct {
	int lastTick;
	int padding; //needed to maintain 4 byte chunk .  reserved for future use.
} rtcStore;

rtcStore rtcMem;
int buckets;
bool toggleFlag;
*/


//holds the offsets for each eeprom data point
// [0]- start index , [1] - maxsize
const struct eepromData {
	int ssid[1][2] = { { 0,32 } };
	int password[1][2] = { { 32,96 } };
	int myhostname[1][2] = { { 96,197 } };
	int lastsetup = 500;
} EEPROMData;


char* htmlBody_save = "<meta http-equiv='refresh' content='0; url = http://10.0.0.1/' /><h1>Saved</h1><br/> Unit is restarting. It should automatically connect to your WiFi. If it can't, you will need to press the setup button while power cycling the device to access setup mode again.\n";
int ISAP = 1;



// prototypes
String macToStr(const uint8_t* mac);
String intToString(int num);
void sdebug(String msg);
String readEEPROM(int start, int size);
void writeEEPROM(String data, int offset);

//on/off callbacks 
void flightOn();
void flightOff();
void ffanLow();
void ffanHi();
void ffanMed();
void ffanOff();

boolean wifiConnected = false;

UpnpBroadcastResponder upnpBroadcastResponder;

Switch *light = NULL;
Switch *fanOff = NULL;
Switch *fanLow = NULL;
Switch *fanMed = NULL;
Switch *fanHi = NULL;


void setup()
{
#ifdef MYDEBUG
	Serial.begin(115200);
#endif // MYDEBUG	
	EEPROM.begin(512);
	delay(10);
	//set setup button to input
	pinMode(AP_PIN, INPUT);
	//Set relay pins to outputs
	setPinModes();

	readConfigs();

	//Check if setup button is pushed
	initialize();

	//Check if setup mode initialized
	if (SETUPMODE==false) {
		// Initialise wifi connection
		wifiConnected = WiFiConnect();

		//Test If OTA requested
		testForOTA();

		if (wifiConnected) {
			if (OTAMode != true) {
				sdebug("\nEnterning normal mode");
				startAlexaCntrl();
			}
			else {
				sdebug("Waiting in OTA mode");
			}
		}
	}

}


void loop()
{

	while (OTAMode == true) {
		ArduinoOTA.handle();
		delay(100);
	}

	//In setupMode
	if (SETUPMODE) {
		server.handleClient();
		delay(100);
	}
	else {
		//In normal mode
		//sdebug("\nEnterning normal mode");
		if (wifiConnected) {
			upnpBroadcastResponder.serverLoop();

			light->serverLoop();
			fanHi->serverLoop();
			fanMed->serverLoop();
			fanLow->serverLoop();
			fanOff->serverLoop();
		}
	}

}

void setPinModes() {
	sdebug("Setting pins to OUTPUT\n");
	pinMode(relayLight, OUTPUT);
	pinMode(relayfanOff, OUTPUT);
	pinMode(relayfanMed, OUTPUT);
	pinMode(relayfanLow, OUTPUT);
	pinMode(relayfanHi, OUTPUT);
	sdebug("Setting pins to LOW\n");

	digitalWrite(relayLight, LOW);
	digitalWrite(relayfanOff, LOW);
	digitalWrite(relayfanMed, LOW);
	digitalWrite(relayfanLow, LOW);
	digitalWrite(relayfanHi, LOW);
}


void startAlexaCntrl() {
	upnpBroadcastResponder.beginUdpMulticast();

	// Define your switches here. Max 14
	// Format: Alexa invocation name, local port no, on callback, off callback
	//say Turn on lightOne
	String RoomName = readEEPROM(EEPROMData.myhostname[0][0], EEPROMData.myhostname[0][1]);

	light = new Switch(RoomName +" Fan Light", 80, flightOn, flightOff);
	fanLow = new Switch(RoomName + " Fan Low", 81, ffanLow, ffanOff);
	fanHi = new Switch(RoomName + " Fan High", 82, ffanHi, ffanOff);
	fanMed = new Switch(RoomName + " Fan Medium", 83, ffanMed, ffanOff);
	fanOff = new Switch(RoomName + " Fan Off", 84, ffanOff, ffanOff);

	Serial.println("Adding switches upnp broadcast responder");
	upnpBroadcastResponder.addDevice(*light);
	upnpBroadcastResponder.addDevice(*fanHi);
	upnpBroadcastResponder.addDevice(*fanLow);
	upnpBroadcastResponder.addDevice(*fanMed);
	upnpBroadcastResponder.addDevice(*fanOff);

	

}


void flightOn() {
	Serial.print("Switch 1 turn Light On ...");
	push(relayLight);   // sets relayOne on
}


void flightOff() {
	Serial.print("Switch 1 turn Light Off ...");
	push(relayLight);   // sets relayOne on
}

void ffanOff() {
	Serial.print("Switch fan on ...");
	push(relayfanOff);   
}

void ffanHi() {
	Serial.print("Switch fan Hi...");
	push(relayfanHi);   // sets relayOne on
}

void ffanMed() {
	Serial.print("Switch fan Med...");
	push(relayfanMed);   // sets relayOne on
}

void ffanLow() {
	Serial.print("Switch fan Low...");
	push(relayfanLow);   // sets relayOne on
}



void push(int button) {
	sdebug("Pushed button " + button);
	sdebug("\n");
	digitalWrite(button, HIGH);
	delay(500);
	digitalWrite(button, LOW);
}


void RemoveSpaces(char* source)
{
	char* i = source;
	char* j = source;
	while (*j != 0)
	{
		*i = *j++;
		if (*i != ' ')
			i++;
	}
	*i = 0;

}

//////

bool WiFiConnect() {
	String clientMac = "";
	unsigned char mac[6];
	WiFi.macAddress(mac);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	clientMac += macToStr(mac);
	sdebug("Reading EEPROM myhostname=");
	myhostname = readEEPROM(EEPROMData.myhostname[0][0], EEPROMData.myhostname[0][1]);
	myhostname.replace(" ", "");
	myhostname = "AlexaFan-"+ myhostname;
	sdebug(myhostname + "\n");

	if (WiFi.status() != WL_CONNECTED)
	{
		sdebug("Not Connected to WIFI...Attempting \n");
		WiFi.hostname(myhostname.c_str());
		WiFi.begin(esid.c_str(), epass.c_str());
		{
			delay(2000);
			if (WiFi.status() != WL_CONNECTED)
			{
				sdebug("Not Connected to WIFI...Attempting \n");
				WiFi.hostname(myhostname);
				WiFi.begin(esid.c_str(), epass.c_str());
				delay(5000);
			}
			else {
				sdebug("    Connected to WIFI\n");
				sdebug(WiFi.localIP().toString()); sdebug(" ");sdebug(clientMac + "\n");
			}
		}
	}
	else {

		sdebug("   Connected to WIFI ");
		sdebug(WiFi.localIP().toString()); sdebug(" ");sdebug(clientMac + "\n");
	}
	if (WiFi.status() != WL_CONNECTED)
	{
		sdebug("Failed to connect to Wifi");
		//sdebug("restarting in " + intToString(sleepTime));sdebug(" seconds \n");
		ESP.restart();
		delay(100);
	}
	else {
		sdebug("    Connected to WIFI\n");
		sdebug(WiFi.localIP().toString()); sdebug(" ");sdebug(clientMac + "\n");
	}

	return (WiFi.status() == WL_CONNECTED);
}



String intToString(int num) {
	char buffer[10];
	itoa(num, buffer, 10);
	return buffer;
}


void sdebug(String msg) {

#ifdef MYDEBUG
	Serial.print(msg);
#endif // DEBUG


}


String macToStr(const uint8_t* mac)
{
	String result;
	for (int i = 0; i < 6; ++i) {
		result += String(mac[i], 16);
		if (i < 5)
			result += ':';
	}
	return result;
}

///
// reads the eeprom at start of size and returns a string
//
String readEEPROM(int start, int size) {
	String buff;
	for (int i = start; i < size; ++i)
	{
		int j = EEPROM.read(i);
		if (j > 0)
			buff += char(j);
		else
			break;
	}
	return buff;
}


void writeEEPROM(String data, int offset) {
	for (int i = 0; i < data.length(); ++i)
	{
		EEPROM.write(i + offset, data[i]);
		sdebug("Wrote: ");
		sdebug(intToString(data[i]));
	}
	sdebug("\n");
}


void setupOTA() {
	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);
	WiFi.mode(WIFI_STA);
	//Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(myhostname.c_str());

	//No authentication by default
	//ArduinoOTA.setPassword((const char *)"123");

	ArduinoOTA.onStart([]() {
		Serial.println("Start");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();
	Serial.println("Ready");
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
}


void factoryReset() {

	String SSID = server.arg("SSID");
	String PASSWORD = server.arg("PASSWORD");

	delay(1000);
	sdebug("clearing eeprom\n");
	for (int i = 0; i < 512; ++i) { EEPROM.write(i, 0); }

	EEPROM.commit();
	server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url = http://10.0.0.1/' />RESETTING DEVICE TO FACTORY DEFAULTS");
	//reboot
	delay(1000);
	ESP.restart();
	yield();
}


void setupMode() {
	sdebug("Entering Setup mode \n");
	const byte DNS_PORT = 53;
	IPAddress apIP(10, 0, 0, 1);    // Private network for server
	SETUPMODE = true;

	String deviceID = "AlexaCntrl" + String(ESP.getChipId());
	WiFi.mode(WIFI_AP);
	delay(100);
	WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
	WiFi.softAP(deviceID.c_str());

	//Setup Captive portal
	// if DNSServer is started with "*" for domain name, it will reply with
	// provided IP to all DNS request
	//DNSServer dnsServer;
//	dnsServer.start(DNS_PORT, "*", apIP);

	sdebug("visit: ");
	sdebug(apIP + "\n");
	server.on("/", handleRoot);
	server.on("/savewifi", handleSaveWifi);
	server.on("/factoryreset", factoryReset);
	server.begin();
	sdebug("HTTP server begin");
	delay(1000);
}


void handleRoot() {
	String clientMac = "";
	unsigned char mac[6];
	WiFi.macAddress(mac);
	clientMac += macToStr(mac);
	server.send(200, "text/html", "<h1>Configuration</h1>"
		"<h2>Wifi Config [" + clientMac + "]</h2>"
		"<form action='savewifi' method='POST'>"
		" SSID:"
		" <input type='text' name='SSID' value='" + esid + "' maxlength='31'>"
		" <br>"
		" Password:"
		"<input type='text' name='PASSWORD' value='" + epass + "' maxlength='65' >"
		" <br><br>"
		"<h2>General Config</h2>"
		" Room:"
		" <input type='text' name='HOSTNAME' value='" + myhostname + "' maxlength='100'>"
		" <br><br>"
		" <input type='submit' value='Save'>"
		"</form>"
		"<br>"
		"<br>"
		"<br>"
		"<form action='factoryreset' method='POST'>"
		" <input type='hidden' name='RESET' value='YES' maxlength='31'>"
		" <input type='submit' value='FACTORY RESET'>"
		"</form>"
		"<br>"
		"Version" + Version
	);
}


void handleSaveWifi() {

	String SSID = server.arg("SSID");
	String PASSWORD = server.arg("PASSWORD");
	String SERVER = server.arg("SERVER");
	String port = server.arg("PORT");
	String sleeptime = server.arg("SLEEPTIME");
	String HOSTNAME = server.arg("HOSTNAME");


	delay(1000);
	sdebug("clearing eeprom\n");
	for (int i = 0; i < 314; ++i) { EEPROM.write(i, 0); }

	sdebug("writing ssid to eeprom\n");
	writeEEPROM(SSID, EEPROMData.ssid[0][0]);

	sdebug("writing password to eeprom\n");
	writeEEPROM(PASSWORD, EEPROMData.password[0][0]);

	sdebug("writing hostname to eeprom\n");
	writeEEPROM(HOSTNAME, EEPROMData.myhostname[0][0]);

	//MARK initial setup complete
	EEPROM.write(EEPROMData.lastsetup, 1);

	EEPROM.commit();

	delay(100);
	server.send(200, "text/html", htmlBody_save);

	//reboot
	ESP.restart();
	delay(100);
	yield();
}




void testForOTA()
{
	WiFiClient client1;
	t_httpUpdate_return ret;

	//Test port 9996 to see if we should go into setup mode.
	sdebug("Testing OTA Setup on: ");sdebug(OTA_HOST.c_str());sdebug(" on Port ");sdebug(intToString(OTA_SETUP) + "\n");
	if (!client1.connect(OTA_HOST.c_str(), OTA_SETUP)) {
		sdebug(">>>NO OTA Setup Available\n");
	}
	else {
		sdebug("Success");
//		EEPROM.write(EEPROMData.lastsetup, 0);
//		EEPROM.commit();
		sdebug("Starting OTA\n");
		setupOTA();
		OTAMode = true;
		return;
	}

	sdebug("Testing OTA on: ");sdebug(OTA_HOST.c_str());sdebug(" on Port ");sdebug(intToString(OTA_REQ_PORT) + "\n");
	if (!client1.connect(OTA_HOST.c_str(), OTA_REQ_PORT)) {
		sdebug(">>>NO OTA Available\n");
		return;
	}
	else {
		sdebug("Success");
		ret = ESPhttpUpdate.update("http://" + OTA_HOST + "/bins/espbins/alexa/fancontrol.bin");
		delay(500);
		switch (ret) {
		case HTTP_UPDATE_FAILED:
			sdebug("HTTP_UPDATE_FAILED Error :"+ ESPhttpUpdate.getLastErrorString());
			client1.print(myhostname + ",HTTP_UPDATE_FAILED Update Failed "+ ESPhttpUpdate.getLastErrorString());
			break;

		case HTTP_UPDATE_NO_UPDATES:
			sdebug("HTTP_UPDATE_NO_UPDATES  :" + ESPhttpUpdate.getLastErrorString());
			client1.print(myhostname + ", HTTP_UPDATE_NO_UPDATES " + ESPhttpUpdate.getLastErrorString());
			break;

		case HTTP_UPDATE_OK:
			sdebug("HTTP_UPDATE_OK  :" + ESPhttpUpdate.getLastErrorString());
			client1.print(myhostname + ", HTTP_UPDATE_OK " + ESPhttpUpdate.getLastErrorString());
			break;
		}
		delay(500);
		ESP.restart();
	}


	client1.print(myhostname + "," + WiFi.localIP().toString() + "Requesting OTA status " + intToString(ret));
	delay(10);
	while (client1.available()) {
		String line = client1.readStringUntil('\r');

	}



	client1.stop();
}


void readConfigs() {
	sdebug("\nReading EEPROM ssid=");
	esid = readEEPROM(EEPROMData.ssid[0][0], EEPROMData.ssid[0][1]);
	sdebug(esid + "\n");

	sdebug("Reading EEPROM password=");
	epass = readEEPROM(EEPROMData.password[0][0], EEPROMData.password[0][1]);
	sdebug(epass + "\n");

	sdebug("Reading EEPROM myhostname=");
	myhostname = readEEPROM(EEPROMData.myhostname[0][0], EEPROMData.myhostname[0][1]);
	sdebug(myhostname + "\n");
}


void initialize() {
	//Check if setup button is pushed
	ISAP = digitalRead(AP_PIN);

	//If system was never setup, go to setup mode right away
	if (EEPROM.read(EEPROMData.lastsetup) != 1) {
		sdebug("\nESP Was never setup before");
		ISAP = 0;
	}
	else {
		sdebug("\nESP Already setup.");
	}

	sdebug("\nRead AP_PIN=");
	sdebug(intToString(ISAP));

   
	//IF AP button pressed (i.e. Setup) start webserver
	//Setup button is D2+3v(10k) and shorted with ground to activate
	if (ISAP == 0) {
		//if button held for more than 10 seconds factory reset
		delay(10000);
		//check button again. if still being held you're in OTA mode
		ISAP = digitalRead(AP_PIN);
		sdebug("Checking for factory reset: " + intToString(ISAP) + "\n");
		if (ISAP) {
			sdebug("\n>>>>>Entering setup mode\n");
			ISAP = 0;
			setupMode();
		}
		else {
			sdebug(">>>>Factory reset mode\n");
			factoryReset();
		}
	}
}
