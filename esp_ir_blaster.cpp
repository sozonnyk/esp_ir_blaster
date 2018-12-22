// Do not remove the include below
#include "esp_ir_blaster.h"

#include "FS.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <vector>
#include <map>
#include <queue>

#include "credentials.h"

#if !defined(WIFI_SSID) && !defined(WIFI_PASS)
	#error "You must define WIFI_SSID and WIFI_PASS in credentials.h"
#endif

#define DBG

#define D(input) {Serial.print(input); Serial.flush();}
#define Dln(input) {Serial.println(input); Serial.flush();}

#define HOSTNAME "ir-blaster"
#define CONFIG_FILE "/esp_ir_blaster.conf"
#define IR_LED 4  // ESP8266 GPIO pin to use. Recommended: 4 (D2).
#define LED 16 //D0
#define IN_1 5
#define IN_2 14

#define IN_1_LABEL "@IN_1"
#define IN_2_LABEL "@IN_2"

#define STARTUP_TIMEOUT 20000

ESP8266WebServer server(80);
String configString;
IRsend irsend(IR_LED);

std::map<String, std::vector<String>> config;

std::vector<String> queue;

String in_1_label = String(IN_1_LABEL);
String in_2_label = String(IN_2_LABEL);

void configInit() {

	String result = "";
	if (SPIFFS.exists(CONFIG_FILE)) {
#ifdef DBG
		Dln("Reading config");
#endif
		config.clear();
		File f = SPIFFS.open(CONFIG_FILE, "r");
		String current;
		while (f.available()) {
			String line = f.readStringUntil('\n');
			result += line + '\n';

			line = line.substring(0, line.indexOf("#"));
			line.trim();

			if (line.length() == 0)
				continue;

			if (line.startsWith("/") || line.startsWith("@")) {
				std::vector<String> vector;
				config[line] = vector;
				current = line;
			} else {
				config.find(current)->second.push_back(line);
			}

#ifdef DBG
			Dln(line);
#endif

		}
	} else {
		File f = SPIFFS.open(CONFIG_FILE, "w");
		f.print("\n");
		f.close();
	}
	configString = result;

#ifdef DBG
		Dln("Config loaded");
#endif
}

void saveConfigString(String text) {
	File f = SPIFFS.open(CONFIG_FILE, "w");
	f.print(text);
	f.close();
}

String index() {
    String sketchSize = String(ESP.getSketchSize());
    String freeSketchSize = String(ESP.getFreeSketchSpace());
    String freeHeap = String(ESP.getFreeHeap());

	String serverIndex = "<html style='font-size: 20px'>";
	serverIndex += "Sketch size: " + sketchSize + " bytes out of " + freeSketchSize + " bytes</br>";
	serverIndex += "Free heap: " + freeHeap + " bytes</br></br>";
	serverIndex += "<form method='POST' id='form' action='/'>";
	serverIndex += "<textarea rows='20' cols='60' name='text' style='font-size: 20px'>";
	serverIndex += configString;
	serverIndex += "</textarea></br></br><input type='submit' value='Submit'></form></html>";
	return serverIndex;
}

void emit(String str) {

	char buffer[str.length() + 1];
	str.toCharArray(buffer, str.length() + 1);

	unsigned long time = 0;
	int n = sscanf(buffer, "P%ld", &time);

	// Pause
	if (n == 1) {
#ifdef DBG
		D("Pause ");
		Dln(time);
#endif
		delay(time);
	}

	unsigned long value1 = 0, value2 = 0;
    n = sscanf(buffer, "%lx:%lx", &value1, &value2);

	switch (n) {
	case 1:
		//NEC code
#ifdef DBG
		D("NEC ");
		Dln(value1);
#endif
		irsend.sendNEC(value1, NEC_BITS);
		break;
	case 2:
		// Panasonic code
#ifdef DBG
		D("Pana ");
		D(value1);
		D(":");
		Dln(value2);
#endif
		irsend.sendPanasonic(value1, value2);
		break;
	}
}

void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";
	for (uint8_t i = 0; i < server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	server.send(404, "text/plain", message);
}

boolean executeCommand(String cmd) {
#ifdef DBG
		D("Requested key: ");
		Dln(cmd);
#endif
	auto search = config.find(cmd);
	if (search != config.end()) {
#ifdef DBG
		D("Found key: ");
		Dln(search->first);
#endif
		for (String cmd : search->second) {
			emit(cmd);
		}
		return true;
	} else {
		return false;
	}
}

void handleAll() {
	queue.push_back(server.uri());
	server.send(200, "text/plain", "OK");
}

void fsInit() {
	SPIFFS.begin();
	if (!SPIFFS.exists("/initialized")) {
#ifdef DBG
		Dln("SPIFFS formatting");
#endif
		SPIFFS.format();
		File f = SPIFFS.open("/initialized", "w");
		if (f) {
			f.println("Format Complete");
			f.close();
		}
	} else {
#ifdef DBG
		Dln("SPIFFS OK");
#endif
	}
}

void wifiInit() {
	pinMode(LED, OUTPUT);
	digitalWrite(LED, LOW);
	WiFi.disconnect();
	WiFi.mode(WIFI_STA);
	WiFi.hostname(HOSTNAME);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	bool led = false;
	unsigned long startTime = millis();
	while (WiFi.status() != WL_CONNECTED) {
		digitalWrite(LED, (led = !led)?LOW:HIGH);
		delay(200);
#ifdef DBG
		D(".");
#endif
		unsigned long int startupTime = millis() - startTime;
		if (startupTime > STARTUP_TIMEOUT) {
#ifdef DBG
			D("Unable to connect to wifi witnin ");
			D(STARTUP_TIMEOUT)
			Dln(" millis. Restarting.")
#endif
			ESP.restart();
		}
	}
#ifdef DBG
	D("Connected to ");
	Dln(WIFI_SSID)
	D("IP address: ");
	Dln(WiFi.localIP());
#endif
	digitalWrite(LED, HIGH);
}

void serverInit() {
	server.on("/", HTTP_GET, []() {
		server.sendHeader("Connection", "close");
		server.send(200, "text/html", index());
	});

	server.on("/", HTTP_POST, []() {
		for (uint8_t i = 0; i < server.args(); i++) {
			if(server.argName(i) == "text") {
				String text = server.arg(i);
				saveConfigString(text);
				configInit();
			}
		}
		server.sendHeader("Connection", "close");
		server.send(200, "text/html", index());
	});

	server.onNotFound(handleAll);
}

unsigned long lastInterrupt_1 = 0;
unsigned long lastInterrupt_2 = 0;

#define INTERRUPT_THRESHOLD 100
// No Serial in ISR, it'll hang otherwise
void ICACHE_RAM_ATTR interrupt_1() {
	if (millis() - lastInterrupt_1 < INTERRUPT_THRESHOLD) {
		return;
	}
	lastInterrupt_1 = millis();
	byte in_1 = digitalRead(IN_1);
	queue.push_back(in_1_label + "_" + (in_1 ? "DOWN" : "UP" ));
}

void ICACHE_RAM_ATTR interrupt_2() {
	if (millis() - lastInterrupt_2 < INTERRUPT_THRESHOLD) {
		return;
	}
	lastInterrupt_2 = millis();
	byte in_2 = digitalRead(IN_2);
	queue.push_back(in_2_label + "_" + (in_2 ? "DOWN" : "UP"));
}

void inputInit(){
	pinMode(IN_1, INPUT);
	pinMode(IN_2, INPUT);
	attachInterrupt(digitalPinToInterrupt(IN_1), interrupt_1, CHANGE);
	attachInterrupt(digitalPinToInterrupt(IN_2), interrupt_2, CHANGE);
	interrupts();
#ifdef DBG
	Dln("Interrupts ready");
#endif
}

void setup(void) {

#ifdef DBG
	Serial.begin(115200);
	Dln("Startup");
#endif
	irsend.begin();
	fsInit();
	configInit();
	wifiInit();
	serverInit();
	server.begin();
	inputInit();

#ifdef DBG
	Dln("Startup complete. Server ready");
#endif
}

void loop(void) {
	server.handleClient();

	if (!queue.empty()) {
		String cmd = queue.front();
		queue.pop_back();
		executeCommand(cmd);
	}
}
