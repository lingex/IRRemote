#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <string>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>
#include <OneButton.h>
#include <IRac.h>
#include <IRtext.h>
#include "WiFiManager.h"

#include "main.h"
#include "ota.h"
#include "rtc_io.h"
#include "icons.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <ir_Hitachi.h>

#include <vector>
#include <map>

using namespace std;

#define SW_ACTIVE (digitalRead(SW_PIN) == 0)
#define USB_ACTIVE (digitalRead(USB_DET) == 1)

#define BAT_SAMPLE_ON  (digitalWrite(BAT_ADCEN, 1))
#define BAT_SAMPLE_OFF (digitalWrite(BAT_ADCEN, 0))


String configFile = "/config.json";
const char* host = "esp32";
const char* ssid = "your ssid";
const char* password = "your passwd";
const char* apssid      = "esp32";
const char* appassword = "pwd4admin";
uint64_t chipid;

int64_t btnChkMs = 0;

const uint16_t lowestBatVol = 3450;	//protect battery voltage
//static std::map<uint32_t, std::pair<uint32_t, const unsigned char *>> batVolIconMap = 
//{
//	//<index, <voltage, icon>>
//	{6, {4050, bat6_icon8x8}},
//	{5, {3950, bat5_icon8x8}},
//	{4, {3880, bat4_icon8x8}},
//	{3, {3760, bat3_icon8x8}},
//	{2, {3700, bat2_icon8x8}},
//	{1, {3620, bat1_icon8x8}},
//	{0, {0,    bat0_icon8x8}}
//};
static std::map<uint32_t, std::pair<uint32_t, const unsigned char *>> batVolIconMap = 
{
	//<index, <voltage, icon>>
	{4, {3980, bat4_icon16x8}},
	{3, {3850, bat3_icon16x8}},
	{2, {3780, bat2_icon16x8}},
	{1, {3620, bat1_icon16x8}},
	{0, {0,    bat0_icon16x8}}
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// The GPIO an IR detector/demodulator is connected to. Recommended: 14 (D5)
// Note: GPIO 16 won't work on the ESP8266 as it does not have interrupts.
// Note: GPIO 14 won't work on the ESP32-C3 as it causes the board to reboot.
#ifdef ARDUINO_ESP32C3_DEV
const uint16_t kRecvPin = 10;  // 14 on a ESP32-C3 causes a boot loop.
#else  // ARDUINO_ESP32C3_DEV
const uint16_t kRecvPin = IR_IN;
#endif  // ARDUINO_ESP32C3_DEV

// GPIO to use to control the IR LED circuit. Recommended: 4 (D2).
const uint16_t kIrLedPin = IR_OUT;

// The Serial connection baud rate.
// NOTE: Make sure you set your Serial Monitor to the same speed.
const uint32_t kBaudRate = 115200;

// As this program is a special purpose capture/resender, let's use a larger
// than expected buffer so we can handle very large IR messages.
const uint16_t kCaptureBufferSize = 1024;  // 1024 == ~511 bits

// kTimeout is the Nr. of milli-Seconds of no-more-data before we consider a
// message ended.
const uint8_t kTimeout = 15;  // Milli-Seconds

// kFrequency is the modulation frequency all UNKNOWN messages will be sent at.
const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.


// Set the smallest sized "UNKNOWN" message packets we actually care about.
// This value helps reduce the false-positive detection rate of IR background
// noise as real messages. The chances of background IR noise getting detected
// as a message increases with the length of the kTimeout value. (See above)
// The downside of setting this message too large is you can miss some valid
// short messages for protocols that this library doesn't yet decode.
//
// Set higher if you get lots of random short UNKNOWN messages when nothing
// should be sending a message.
// Set lower if you are sure your setup is working, but it doesn't see messages
// from your device. (e.g. Other IR remotes work.)
// NOTE: Set this value very high to effectively turn off UNKNOWN detection.
const uint16_t kMinUnknownSize = 12;

// How much percentage lee way do we give to incoming signals in order to match
// it?
// e.g. +/- 25% (default) to an expected value of 500 would mean matching a
//      value between 375 & 625 inclusive.
// Note: Default is 25(%). Going to a value >= 50(%) will cause some protocols
//       to no longer match correctly. In normal situations you probably do not
//       need to adjust this value. Typically that's when the library detects
//       your remote's message some of the time, but not all of the time.
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance is normally 25%

// Legacy (No longer supported!)
//
// Change to `true` if you miss/need the old "Raw Timing[]" display.
#define LEGACY_TIMING_INFO false
// ==================== end of TUNEABLE PARAMETERS ====================

// Use turn on the save buffer feature for more complete capture coverage.
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;  // Somewhere to store the results

// ==================== end of TUNEABLE PARAMETERS ====================

// The IR transmitter.
IRHitachiAc1 ac(kIrLedPin);

WiFiServer server(80);

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

std::vector<OneButton> buttons = {
	OneButton(KEY_BOOT),
	OneButton(KEY_GO),
	OneButton(KEY_UP),
	OneButton(KEY_DOWN),
	OneButton(KEY_LEFT),
	OneButton(KEY_RIGHT),
	OneButton(KEY_OK)
};

uint32_t batteryVol = 0;	//battary voltage in mV
uint16_t idleClock = 0;
const uint16_t sleepClock = 25;

//RTC_DATA_ATTR uint8_t acMode = 0;
uint32_t upCount = 0;
uint16_t oledColor = SSD1306_WHITE;

void handleModeInt();
void handleOkInt();
void handleUpInt();
void handleDownInt();
void handleLeftInt();
void handleRightInt();

void ButtonEventsAttach();
void printState();
void printOLED();
uint32_t GetBatteryVol();

void ButtonActionGo();
void ButtonActionOk();
void ButtonActionMode();
void ButtonActionUp();
void ButtonActionDown();
void ButtonActionLeft();
void ButtonActionRight();
void LowBatteryAction();
void AcCmdSend();
void AcSwingVSwitch();
void AcPowerToggle();
String GetDeviceInfoString();

uint16_t GetOledColor()
{
	uint16_t color = SSD1306_WHITE;
	if (upCount % 10 >= 5)
	{
		color = SSD1306_BLACK;
	}
	return color;
}

void AcBackup()
{
	Serial.println("AC state backup.");

	String data = "";
	DynamicJsonDocument doc(1024);

	if(SPIFFS.exists(configFile))
	{
		File file = SPIFFS.open(configFile, FILE_READ);
		String data = file.readString();
		deserializeJson(doc, data);
		auto pJson = doc.getOrAddMember("ac").as<JsonObject>();
		pJson["mode"].set<int>(ac.getMode());
		pJson["fan"].set<int>(ac.getFan());
		pJson["temp"].set<int>(ac.getTemp());

		doc["upCount"] = ++upCount;
		file.close();
	}

	File fileW = SPIFFS.open(configFile, FILE_WRITE);
	serializeJson(doc, fileW);
	fileW.close();
}

String AcModeString(uint8_t mode)
{
	String result = "UNKNOW";
	switch (mode)
	{
	case kHitachiAc1Cool:
		result = "Cool";
		break;
	case kHitachiAc1Heat:
		result = "Heat";
		break;
	case kHitachiAc1Dry:
		result = "Dry";
		break;
	case kHitachiAc1Fan:
		result = "Fan";
		break;
	case kHitachiAc1Auto:
		result = "Auto";
		break;
	default:
		break;
	}
	return result;
}

String AcFanString(uint8_t speed)
{
	String result = "UNKNOW";
	switch (speed)
	{
	case kHitachiAc1FanHigh:
		result = "High";
		break;
	case kHitachiAc1FanMed:
		result = "Med";
		break;
	case kHitachiAc1FanLow:
		result = "Low";
		break;
	case kHitachiAc1FanAuto:
		result = "Auto";
		break;
	default:
		break;
	}
	return result;
}

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason()
{
	esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

	switch(wakeup_reason)
	{
		case ESP_SLEEP_WAKEUP_EXT0:
		{
			Serial.println("Wakeup caused by external signal using RTC_IO");
		}
		break;
		case ESP_SLEEP_WAKEUP_EXT1:
		{
			Serial.println("Wakeup caused by external signal using RTC_CNTL");
		}
		break;
		case ESP_SLEEP_WAKEUP_TIMER:
		{
			Serial.println("Wakeup caused by timer");
		}
		break;
		case ESP_SLEEP_WAKEUP_TOUCHPAD:
		{
			Serial.println("Wakeup caused by touchpad");
		}
		break;
		case ESP_SLEEP_WAKEUP_ULP:
		{
			Serial.println("Wakeup caused by ULP program");
		}
		break;
		default:
		{
			Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason);
		}
		break;
	}
}

String acPowerState()
{
	return (ac.getPower() ? "checked" : "");
}

String GetContentType(String filename)
{
  //if(server.hasArg("download")) return "application/octet-stream";
  if(filename.startsWith("/u/")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
/*
void OnNotFound(AsyncWebServerRequest *request)
{
  String message = "";
  String path = request->url();
  Serial.print("request to: " + path + "\n");
  String contentType = GetContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {
    if(SPIFFS.exists(pathWithGz))
	{
      path += ".gz";
    }
	AsyncWebServerResponse *response =  request->beginResponse(SPIFFS, path, contentType);
	request->send(response);
    return;
  }

  Serial.print("File not Found:");
  Serial.println(path);
  message += "404 The content you are looking for was not found.\n\n";
  message += "Path: ";
  message += path;
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArgs: ";
  message += request->args();
  message += "\n";
  for(uint8_t i = 0; i < request->args(); i++)
  {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }
  request->send_P(404, "text/plain", message.c_str());
}
*/

String GetSpiffsFile(String fileName)
{
  String message = "";
  String path = fileName;
  Serial.print("request to: " + path + "\n");
  String contentType = GetContentType(fileName);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {
    if(SPIFFS.exists(pathWithGz))
	{
      path += ".gz";
    }
	File file = SPIFFS.open(path, FILE_READ);
	char *buf = new char[file.size()];
	memset(buf, 0, file.size());
	file.readBytes(buf, file.size());

	String str = String(buf);
	delete[] buf;
    return str;
  }
  return "";
}

bool ConnectWiFi(String ssid,String pwd)
{
	Serial.println("Trying WiFi: " + ssid);
	int connectCount = 0;
	WiFi.begin(ssid.c_str(), pwd.c_str());
	while(!WiFi.isConnected() && connectCount++ < 9)
	{
		delay(500);
		Serial.print(".");
	}
	if(WiFi.isConnected())
	{
		Serial.println("");
		Serial.print("Connected to WiFi: ");
		Serial.println(ssid);
		Serial.print("IP address: ");
		Serial.println(WiFi.localIP());
		connectCount = 0;
		WiFi.setAutoReconnect(true);
		MDNS.addService("http", "tcp", 80);
		return true;
	}
	Serial.println("Connect fail!");
	return false;
}

void setup()
{
	pinMode(LED, OUTPUT);
	digitalWrite(LED, 1);

	// Setup serial interface
	Serial.begin(115200);
	delay(10);

	print_wakeup_reason();

	/*After waking up from sleep, the IO pad used for wakeup will be configured as RTC IO. 
	Therefore, before using this pad as digital GPIO, 
	users need to reconfigure it using rtc_gpio_deinit() function.
	*/
	rtc_gpio_deinit((gpio_num_t)KEY_OK);
	rtc_gpio_deinit((gpio_num_t)SW_PIN);
	rtc_gpio_hold_dis(GPIO_NUM_2);	//reconnect I2C_SCL pin
	BAT_SAMPLE_OFF;
	pinMode(SW_PIN, INPUT);
	pinMode(USB_DET, INPUT);
	pinMode(BAT_ADCEN, OUTPUT);

	//Serial.printf("AC model:%s.\n", "R_LT0541_HTA_B");
	ac.begin();
	ac.setModel(R_LT0541_HTA_B);
	ac.setSleep(0);
	ac.setPower(false);
	ac.setPowerToggle(false);
	ac.setSwingToggle(false);
	ac.setSwingH(false);
	ac.setSwingV(false);

	SPIFFS.begin();
	if(SPIFFS.exists(configFile))
	{
		File file = SPIFFS.open(configFile, FILE_READ);
		String data = file.readString();
		DynamicJsonDocument doc(1024);
		deserializeJson(doc, data);
		JsonObject pObj = doc.getMember("ac").as<JsonObject>();

		ac.setMode(pObj["mode"].as<int>());
		ac.setFan(pObj["fan"].as<int>());
		ac.setTemp(pObj["temp"].as<int>());

		upCount = doc.getOrAddMember("upCount").as<uint32_t>();
		Serial.printf("device upCount: %d\n", upCount);
		file.close();
	}
	else
	{
		ac.setMode(kHitachiAc1Cool);
		ac.setTemp(kHitachiAc1TempAuto);
		ac.setFan(kHitachiAc1FanLow);
		AcBackup();
	}

	Wire.setPins(DIS_SDA, DIS_SCL);
	if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
	{
		Serial.println(F("SSD1306 allocation failed"));
		//for(;;); // Don't proceed, loop forever
	}
	oledColor = GetOledColor();
	display.invertDisplay((oledColor == SSD1306_BLACK) ? true : false);
/*
	//the adafruit logo
	display.display();
	delay(100);
*/
	display.clearDisplay();
	// text display tests
	display.setTextSize(2);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(14, 7);
	display.println("AC Remote");
	display.display();

	if (SW_ACTIVE)
	{
		if(SPIFFS.exists(configFile))
		{
			File file = SPIFFS.open(configFile, "r");
			String data = file.readString();
			Serial.print("loading config:");
			Serial.println(data);

			DynamicJsonDocument doc(1024);
			deserializeJson(doc, data);
			JsonArray arr = doc.getMember("wifi").as<JsonArray>();
			Serial.print("wifi configs:");
			Serial.println(arr.size());
			for(int i=0; i<arr.size(); i++)
			{
				if(ConnectWiFi(arr[i]["ssid"], arr[i]["pwd"]))
				{
					break;
				}
			}
			file.close();
		}
		else
		{
			Serial.println("File not found:" + configFile);
		}

		if(!WiFi.isConnected())
		{
			if(!ConnectWiFi(ssid, password))
			{
				WiFiManager wifiManager;

				wifiManager.resetSettings();
				//in seconds
				wifiManager.setTimeout(300);
				
				//set custom ip for portal
				//wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

				//fetches ssid and pass from eeprom and tries to connect
				//if it does not connect it starts an access point with the specified name
				//here  "AutoConnectAP"
				//and goes into a blocking loop awaiting configuration
				if (wifiManager.autoConnect(apssid, appassword))
				{
					Serial.println("failed to connect and hit timeout");
					delay(3000);
					//reset and try again, or maybe put it to deep sleep
					ESP.restart();
				}
				//or use this for auto generated name ESP + ChipID
				//wifiManager.autoConnect();
			}
		}

		server.begin();
		openOTA();

		/*use mdns for host name resolution*/
		if (!MDNS.begin(host))
		{ //http://esp32.local
			Serial.println("Error setting up MDNS responder!");
			while (1) 
			{
				delay(1000);
			}
		}
		Serial.println("mDNS responder started");

#if DECODE_HASH
		// Ignore messages with less than minimum on or off pulses.
		irrecv.setUnknownThreshold(kMinUnknownSize);
#endif  // DECODE_HASH
		irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.
		irrecv.enableIRIn(false);  // Start the receiver
	}

	chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
	Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
	Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.

	ButtonEventsAttach();

	batteryVol = GetBatteryVol();

	printState();
	digitalWrite(LED, 0);
}

const unsigned char * GetBatteryIcon()
{
	const unsigned char * pIcon = batVolIconMap[0].second;
	int64_t curSec = millis() / 1000;
	uint8_t index = 0;
	static uint8_t chargeStep;
	static int64_t lastSec = curSec;

	for (auto it = batVolIconMap.rbegin(); it != batVolIconMap.rend(); it++)
	{
		auto pair = it->second;
		if (batteryVol >= pair.first)
		{
			index = it->first;
			pIcon = pair.second;
			break;
		}
	}
	if (USB_ACTIVE && batVolIconMap.size() > 0)	//charging
	{
		if (curSec != lastSec)
		{
			lastSec = curSec;
			if (chargeStep < index)
			{
				chargeStep = index;
			}
			chargeStep++;
			if (chargeStep >= batVolIconMap.size())
			{
				chargeStep = index;
			}
			index = chargeStep;
			pIcon = batVolIconMap[index].second;
		}
	}

	return pIcon;
}

void printOLED()
{
	display.clearDisplay();

	//MODE
	display.setTextSize(1);
	display.setCursor(1, 4);
	display.printf("Mode:%s", AcModeString(ac.getMode()).c_str());

	//TEMPERATURE
	display.setTextSize(2);
	display.setCursor(60, 16);
	display.printf("%dC", ac.getTemp());

	display.setTextSize(1);

	//FAN SPEED
	display.setCursor(1, 23);
	display.printf("Fan:%s", AcFanString(ac.getFan()).c_str());
	//display.drawBitmap(60, 16, GetFanIcon(), 16, 8, SSD1306_WHITE);

	display.setTextSize(1);

	uint8_t iconPlace = 111;
	const uint8_t iconDis = 18;

	//battery icon
	{
	#if 0
		display.setCursor(96, 12);
		display.printf("%0.2fV", batteryVol / 1000.0);
	#endif
		display.drawBitmap(iconPlace, 1, GetBatteryIcon(), 16, 8, SSD1306_WHITE);
		iconPlace -= iconDis;
	}
	//usb icon
	if (USB_ACTIVE)
	{
		//display.drawBitmap(120, 1, bat5_icon8x8, 8, 8, SSD1306_WHITE);	//charge icon

		//display.setCursor(106, 1);
		//display.print("USB");
		display.drawBitmap(iconPlace, 1, usb_icon16x8, 16, 8, SSD1306_WHITE);
		iconPlace -= iconDis;
	}
	//wifi icon
	if (WiFi.isConnected())
	{
		//display.setCursor(72, 12);
		//display.print("WIFI");
		display.drawBitmap(iconPlace, 1, wifi_icon16x8, 16, 8, SSD1306_WHITE);
		iconPlace -= iconDis;
	}
	//sleep countdown
	if (idleClock > 0)
	{
		display.setCursor(116, 24);
		display.printf("%02d", sleepClock - idleClock);
	}
	
	//delay(10);
	display.display(); // actually display all of the above
}

void printState()
{
	printOLED();

	// Display the settings.
	Serial.println("Remote state: ");
	Serial.printf("  %s\n", ac.toString().c_str());
	// Display the encoded IR sequence.
	unsigned char* ir_code = ac.getRaw();
	Serial.print("IR Code: 0x");
	for (uint8_t i = 0; i < kHitachiAc1StateLength; i++)
	{
		Serial.printf("%02X", ir_code[i]);
	}
	Serial.println();
}

//battery voltage in mV
uint32_t GetBatteryVol()
{
	const uint16_t fixVal = 25;//mV
	static vector<uint16_t> adcVec;
	uint32_t count = 0;

	BAT_SAMPLE_ON;
	delay(4);
	//adcVec.push_back(analogRead(BAT_ADC));
	adcVec.push_back(analogReadMilliVolts(BAT_ADC) - fixVal);
	BAT_SAMPLE_OFF;

	if (adcVec.size() > 5)
	{
		adcVec.erase(adcVec.begin());
	}
	for (auto const &it : adcVec)
	{
		count += it;
	}

	//Serial.printf("size: %d, count: %d\n", adcVec.size(), count);
	count = count * 2 / adcVec.size();
	return count;
}

void loop()
{
	int64_t curMs = millis();
	static int64_t lastRefresh = 0;
	int64_t epochMills = curMs - lastRefresh;

	if (lastRefresh == 0 || epochMills >= 1000)
	{
		lastRefresh = curMs;
		batteryVol = GetBatteryVol();
		//Serial.printf("batVal: %dmV\n", batteryVol);
		if (!SW_ACTIVE)
		{
			if (batteryVol < lowestBatVol)
			{
				LowBatteryAction();
			}
			idleClock++;
			Serial.printf("Going to sleep in: %d Sec.\n", sleepClock - idleClock);
		}
		else
		{
			idleClock = 0;
		}
		printOLED();
	}
	if (curMs - btnChkMs >= 10)
	{
		btnChkMs = curMs;
		for (auto &it : buttons)
		{
			it.tick();
		}
	}

	if (SW_ACTIVE)
	{
#if 1
		WiFiClient client = server.available();   // Listen for incoming clients
		if (client)
		{                             // If a new client connects,
			// Variable to store the HTTP request
			String header;

			currentTime = millis();
			previousTime = currentTime;
			Serial.println("New Client.");          // print a message out in the serial port
			String currentLine = "";                // make a String to hold incoming data from the client
			while (client.connected() && currentTime - previousTime <= timeoutTime)
			{ // loop while the client's connected
				currentTime = millis();
				if (client.available())
				{             // if there's bytes to read from the client,
					char c = client.read();             // read a byte, then
					Serial.write(c);                    // print it out the serial monitor
					header += c;
					if (c == '\n')
					{                    // if the byte is a newline character
						// if the current line is blank, you got two newline characters in a row.
						// that's the end of the client HTTP request, so send a response:
						if (currentLine.length() == 0)
						{
							uint8_t acMode = ac.getMode();
							uint8_t acFan = ac.getFan();
							uint8_t acTemp = ac.getTemp();
							// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
							// and a content-type so the client knows what's coming, then a blank line:
							client.println("HTTP/1.1 200 OK");
							client.println("Content-type:text/html");
							client.println("Connection: close");
							client.println();

							// Display the HTML web page
							client.println("<!DOCTYPE html><html>");
							client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
							client.println("<link rel=\"icon\" href=\"data:,\">");
							client.println("<style>");
							client.println("html {font-family: Arial; display: inline-block; text-align: center;}");
							client.println("h2 {font-size: 2.4rem;}");
							client.println("p {font-size: 2.2rem;}");
							client.println("body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}");
							client.println(".switch {position: relative; display: inline-block; width: 120px; height: 68px} ");
							client.println(".switch input {display: none}");
							client.println(".slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}");
							client.println(".slider:before {position: absolute; content: ''; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}");
							client.println("input:checked+.slider {background-color: #2196F3}");
							client.println("input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}");
							client.println(".sliderTemp { width: 300px; }");
							client.println(".button { width:120px; height:55px; padding: 15px 25px; font-size: 14px; cursor: pointer; text-align: center; text-decoration: none; outline: none; color: #fff; background-color: #2196F3; border: none; border-radius: 8px; margin: 4px 2px; }");
							client.println(".button:hover { background-color: #0354ce; }");
							client.println(".button:active { background-color: #0b16b3; }");
							client.println("</style>");
							//client.println("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.3.1/jquery.min.js\"></script>");
							//client.println("<script src=\"https://ajax.aspnetcdn.com/ajax/jquery/jquery-3.3.1.min.js\"></script>");
							client.println("<script src=\"https://cdn.staticfile.org/jquery/1.10.2/jquery.min.js\"></script>");
							//client.println("<script src=\"/jquery.js\"></script>");
									
							// Web Page
							client.println("</head><body><h1>Hitachi AC</h1>");

							client.println("<p><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"powerbtn\" " + acPowerState() + "><span class=\"slider\"></span></label></p>");

							client.println("<center>");
							client.println("<fieldset style='width:300px; border-radius: 8px' id=\"fieldsetACMode\" onchange=\"ac()\">");
							client.println("<legend align='center'>Mode</legend>");
							client.println("<input type='radio' name='acMODE' value='" + String(kHitachiAc1Cool) + "' " + String(acMode == kHitachiAc1Cool ? "checked" : "") +  "/>Cool");
							client.println("<input type='radio' name='acMODE' value='" + String(kHitachiAc1Heat) + "' " + String(acMode == kHitachiAc1Heat ? "checked" : "") +  "/>Heat");
							client.println("<input type='radio' name='acMODE' value='" + String(kHitachiAc1Fan) + "' " + String(acMode == kHitachiAc1Fan ? "checked" : "") +  "/>Fan");
							client.println("<input type='radio' name='acMODE' value='" + String(kHitachiAc1Dry) + "' " + String(acMode == kHitachiAc1Dry ? "checked" : "") +  "/>Dry");
							client.println("<input type='radio' name='acMODE' value='" + String(kHitachiAc1Auto) + "' " + String(acMode == kHitachiAc1Auto ? "checked" : "") +  "/>Auto");
							client.println("</fieldset>");

							client.println("<br>");

							client.println("<fieldset style='width:300px; border-radius: 8px' id=\"fieldsetACFan\" onchange=\"ac()\">");
							client.println("<legend align='center'>Fan</legend>");
							client.println("<input type='radio' name='fanSpeed' value='" + String(kHitachiAc1FanLow) + "' " + String(acFan == kHitachiAc1FanLow ? "checked" : "")  + "/>Low");
							client.println("<input type='radio' name='fanSpeed' value='" + String(kHitachiAc1FanMed) + "' " + String(acFan == kHitachiAc1FanMed ? "checked" : "")  + "/>Med");
							client.println("<input type='radio' name='fanSpeed' value='" + String(kHitachiAc1FanHigh) + "' " + String(acFan == kHitachiAc1FanHigh ? "checked" : "")  + "/>High");
							client.println("<input type='radio' name='fanSpeed' value='" + String(kHitachiAc1FanAuto) + "' " + String(acFan == kHitachiAc1FanAuto ? "checked" : "")  + "/>Auto");
							client.println("</fieldset>");
							client.println("</center>");

							//temp slider
							client.println("<p><nobr>Temp: <span id=\"acTemp\"></span>&#x2103</nobr></p>"); //&#x2103: ℃
							client.println("<input type='range' min='16' max='32' class='sliderTemp' id='acTempSlider' onchange='ac()' value='" + String(ac.getTemp()) + "'/>");

							client.println("<p>");
							client.println("<button type=\"button\" class=\"button\" onclick=\"swingAction('1')\">Swing X</button>");
							client.println("<button type=\"button\" class=\"button\" onclick=\"swingAction('2')\">Swing Y</button>");
							client.println("</p>");

							client.println("<div style=\"position: relative;\">");
							client.println("<div style=\"position: absolute; bottom: -300px; background-color: rgb(207, 237, 248)\">");
							client.println(GetDeviceInfoString());
							client.println("</div>");
							client.println("</div>");

							client.println("<script>");
							client.println("var tempSlider = document.getElementById(\"acTempSlider\");");
							client.println("var acT = document.getElementById(\"acTemp\"); acT.innerHTML = tempSlider.value;");
							client.println("tempSlider.oninput = function() { tempSlider.value = this.value; acT.innerHTML = this.value; }");

							client.println("$.ajaxSetup({timeout:1000});");

							client.println("function toggleCheckbox(element) {");
							client.println("if(element.checked){ btnAction(1); }");
							client.println("else { btnAction(0); }");
							client.println("}");

							//action
							client.println("function ac() { ");
							client.println("var modeSelector = document.querySelector('input[name=\"acMODE\"]:checked');");
							client.println("var fanSelector = document.querySelector('input[name=\"fanSpeed\"]:checked');");
							client.println("var modeVal = modeSelector ? modeSelector.value : 1;");
							client.println("var fanVal = modeSelector ? fanSelector.value : 1;");
							client.println("var tempVal = tempSlider ? tempSlider.value : 1;");
							client.println("console.log('mode:' + modeVal + ', fanSpeed:' + fanVal + ', temp:' + tempVal);");
							client.println("var val = modeVal * 10000 + fanVal * 100 + tempVal * 1;");
							//client.println("$.get(\"/?mode=\" + modeVal + \"&\" + \"fan=\" + fanVal + \"&\" + \"temp=\" + tempVal + \"&\"); {Connection: close};}</script>");
							client.println("$.get(\"/?value=\" + val + \"&\"); {Connection: close};");
							client.println("}");

							//swing action
							client.println("function swingAction(btnVal) {");
							client.println("var modeSelector = document.querySelector('input[name=\"acMODE\"]:checked');");
							client.println("var fanSelector = document.querySelector('input[name=\"fanSpeed\"]:checked');");
							client.println("var modeVal = modeSelector ? modeSelector.value : 1;");
							client.println("var fanVal = modeSelector ? fanSelector.value : 1;");
							client.println("var tempVal = tempSlider ? tempSlider.value : 1;");
							client.println("console.log('mode:' + modeVal + ', fanSpeed:' + fanVal + ', temp:' + tempVal + ', swing:' + btnVal);");
							client.println("var val = modeVal * 10000 + fanVal * 100 + tempVal * 1;");
							client.println("$.get(\"/?value=\" + val + \"&swing=\" + btnVal + \"&\"); {Connection: close};");
							client.println("}");

							//power switch action
							client.println("function btnAction(btnVal) {");
							client.println("var modeSelector = document.querySelector('input[name=\"acMODE\"]:checked');");
							client.println("var fanSelector = document.querySelector('input[name=\"fanSpeed\"]:checked');");
							client.println("var modeVal = modeSelector ? modeSelector.value : 1;");
							client.println("var fanVal = modeSelector ? fanSelector.value : 1;");
							client.println("var tempVal = tempSlider ? tempSlider.value : 1;");
							client.println("console.log('mode:' + modeVal + ', fanSpeed:' + fanVal + ', temp:' + tempVal + ', switch:' + btnVal);");
							client.println("var val = modeVal * 10000 + fanVal * 100 + tempVal * 1;");
							client.println("$.get(\"/?value=\" + val + \"&switch=\" + btnVal + \"&\"); {Connection: close};");
							client.println("}");

							client.println("console.log(\"" + GetDeviceInfoString() + "\")");

							client.println("</script></body></html>");
							
							//GET /?value=140125&switch=1& HTTP/1.1
							bool valueOk = false;
							if(header.indexOf("GET /?value=") >= 0)
							{
								int pos1 = header.indexOf('=');
								int pos2 = header.indexOf('&');
								int val = header.substring(pos1 + 1, pos2).toInt();
								acMode = val / 10000;
								acFan = val % 10000 / 100;
								acTemp = val % 10000 % 100;
								if (acTemp < 16 || acTemp > 35)
								{
									acTemp = 25;
								}
								ac.setMode(acMode);
								ac.setFan(acFan);
								ac.setTemp(acTemp);
								valueOk = true;
							}
							if (header.indexOf("GET /favicon.ico"))
							{
								//client.println("HTTP/1.1 200 OK");
								//client.println("Content-type:image/x-icon");
								//client.println(GetSpiffsFile("/favicon.ico"));
								//client.println("Connection: close");
							}
							if(header.indexOf("&swing=") >= 0)
							{
								int pos = header.indexOf("&swing=");
								int val = header.substring(pos + 7, pos + 8).toInt();
								if (val == 1)
								{
									//swing X
									AcSwingVSwitch();
								}
								else if (val == 2)
								{
									//swing Y same function as X right now
									AcSwingVSwitch();
								}
							}
							else if(header.indexOf("&switch=") >= 0)
							{
								int pos = header.indexOf("&switch=");
								int val = header.substring(pos + 8, pos + 9).toInt();
								//AcPowerSwitch(val != 0);
								if (val != ac.getPower())
								{
									AcPowerToggle();
								}
							}
							else if(valueOk)
							{
								AcCmdSend();
							}
							Serial.println(header);
							// The HTTP response ends with another blank line
							client.println();
							// Break out of the while loop
							break;
						}
						else
						{ // if you got a newline, then clear currentLine
							currentLine = "";
						}
					}
					else if (c != '\r')
					{  // if you got anything else but a carriage return character,
						currentLine += c;      // add it to the end of the currentLine
					}
				}
			}
			// Clear the header variable
			header = "";
			// Close the connection
			client.stop();
			Serial.println("Client disconnected.");
			Serial.println("");
		}
#endif

		if (idleClock != 0 && !WiFi.isConnected())
		{
			Serial.println("\nGoing to restart by SW active!");
			ESP.restart();
		}
		//server.handleClient();
		ArduinoOTA.handle();
		// Check if the IR code has been received.
		if (irrecv.decode(&results))
		{
			// Display a crude timestamp.
			uint32_t now = millis();
			Serial.printf(D_STR_TIMESTAMP " : %06u.%03u\n", now / 1000, now % 1000);
			// Check if we got an IR message that was to big for our capture buffer.
			if (results.overflow)
			Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
			// Display the library version the message was captured with.
			Serial.println(D_STR_LIBRARY "   : v" _IRREMOTEESP8266_VERSION_STR "\n");
			// Display the tolerance percentage if it has been change from the default.
			if (kTolerancePercentage != kTolerance)
			Serial.printf(D_STR_TOLERANCE " : %d%%\n", kTolerancePercentage);
			// Display the basic output of what we found.
			Serial.print(resultToHumanReadableBasic(&results));
			// Display any extra A/C info if we have it.
			String description = IRAcUtils::resultAcToString(&results);
			if (description.length()) Serial.println(D_STR_MESGDESC ": " + description);
			yield();  // Feed the WDT as the text output can take a while to print.
#if LEGACY_TIMING_INFO
			// Output legacy RAW timing info of the result.
			Serial.println(resultToTimingInfo(&results));
			yield();  // Feed the WDT (again)
#endif  // LEGACY_TIMING_INFO
			// Output the results as source code
			Serial.println(resultToSourceCode(&results));
			Serial.println();    // Blank line between entries
			yield();             // Feed the WDT (again)
		}
	}
	else if (idleClock >= sleepClock)
	{
		display.clearDisplay();
		// text display tests
		display.setTextSize(1);
		display.setCursor(28, 12);
		display.println("Going to sleep");
		display.display();
		AcBackup();

		Serial.println("Going to sleep now");
		Serial.flush();

		esp_sleep_enable_timer_wakeup(1000 * 1000);	//delay(1000)
		esp_light_sleep_start();

		//power off oled
		display.ssd1306_command(SSD1306_DISPLAYOFF);

		//wait until button release
		while (digitalRead(KEY_OK) == 0)
		{
			esp_sleep_enable_timer_wakeup(100 * 1000);	//wakeup every 100 millisec
			esp_light_sleep_start();
			yield();
		}

		esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY_OK, 0);	//can wake only when rtc active

		uint64_t mask = 0;
		mask |=  1ull << SW_PIN;
		esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);	//can wake when rtc shutdown

		esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
		rtc_gpio_isolate(GPIO_NUM_2);	//pull down inside and pull up as I2C_SCL outside, avoid power consume
		esp_deep_sleep_start();
	}
	yield();

	if (!SW_ACTIVE && !WiFi.isConnected())
	{
		esp_sleep_enable_timer_wakeup(50 * 1000);	//wakeup every 50 millisec
		esp_light_sleep_start();
	}
}

void AcCmdSend()
{
	bool swActive = SW_ACTIVE;

	if (swActive)
	{
		irrecv.disableIRIn();
	}

	printState();
	digitalWrite(LED, 1);
	ac.send();
	digitalWrite(LED, 0);
	//printState();

	if (swActive)
	{
		irrecv.enableIRIn();
	}
}

void AcPowerToggle()
{
	//ac.setPowerToggle(true);
	ac.setPower(ac.getPower() ? false : true);
	AcCmdSend();
}

void AcPowerSwitch(bool on)
{
	ac.setPower(on);
	AcCmdSend();
}

void AcSwingVSwitch()
{
	ac.setSwingToggle(true);
	AcCmdSend();
}

void AcFanSpeed()
{
	uint8_t speed = ac.getFan();
	switch (speed)
	{
	case kHitachiAc1FanAuto:
		speed = kHitachiAc1FanLow;
		break;
	case kHitachiAc1FanHigh:
		speed = kHitachiAc1FanAuto;
		break;
		case kHitachiAc1FanMed:
		speed = kHitachiAc1FanHigh;
		break;
		case kHitachiAc1FanLow:
		speed = kHitachiAc1FanMed;
		break;
	default:
		speed = kHitachiAc1FanAuto;
		break;
	}
	ac.setFan(speed);
	AcCmdSend();
}

void AcModeSwitch()
{
	uint8_t acMode = ac.getMode();
	switch (acMode)
	{
	case kHitachiAc1Dry:
		acMode = kHitachiAc1Fan;
		break;
	case kHitachiAc1Fan:
		acMode = kHitachiAc1Cool;
		break;
		case kHitachiAc1Cool:
		acMode = kHitachiAc1Heat;
		break;
		case kHitachiAc1Heat:
		acMode = kHitachiAc1Auto;
		break;
		case kHitachiAc1Auto:
		acMode = kHitachiAc1Dry;
		break;
	default:
		acMode = kHitachiAc1Fan;
		break;
	}

	if (acMode == kHitachiAc1Fan && ac.getFan() == kHitachiAcFanAuto)//auto fan speed not allow in fan mode
	{
		ac.setFan(kHitachiAcFanLow);
	}
	ac.setMode(acMode);
	AcCmdSend();
}

void ButtonActionGo()
{
	idleClock = 0;
	Serial.println("BTN GO CLICK");
	AcPowerToggle();
}
void ButtonActionOk()
{
	idleClock = 0;
	Serial.println("BTN OK CLICK");

	{
		//power on oled
		display.ssd1306_command(SSD1306_DISPLAYON);
	}
}
void ButtonActionLongOk()
{
	Serial.println("BTN OK HOLD");
	if (!SW_ACTIVE)
	{
		idleClock = sleepClock;
	}
	else
	{
		//power off oled
		display.ssd1306_command(SSD1306_DISPLAYOFF);
	}
}
void ButtonActionMode()
{
	idleClock = 0;
	Serial.println("BTN MODE CLICK");
	AcModeSwitch();
}
void ButtonActionUp()
{
	idleClock = 0;
	Serial.println("BTN UP CLICK");
	ac.setTemp(ac.getTemp() + 1);
	AcCmdSend();
}
void ButtonActionDown()
{
	idleClock = 0;
	Serial.println("BTN DOWN CLICK");
	ac.setTemp(ac.getTemp() - 1);
	AcCmdSend();
}
void ButtonActionLeft()
{
	idleClock = 0;
	Serial.println("BTN LEFT CLICK");
	AcSwingVSwitch();
}
void ButtonActionRight()
{
	idleClock = 0;
	Serial.println("BTN RIGHT CLICK");
	AcFanSpeed();
}

void ButtonEventsAttach()
{
	Serial.println("BTN ATTACHED");
	if (buttons.size() < 1)
	{
		Serial.println("BTN SIZE ERR");
		return;
	}

/*
	OneButton(KEY_BOOT),
	OneButton(KEY_GO),
	OneButton(KEY_UP),
	OneButton(KEY_DOWN),
	OneButton(KEY_LEFT),
	OneButton(KEY_RIGHT),
	OneButton(KEY_OK)
*/
	// mode
	buttons[0].attachClick([] {
		ButtonActionMode();
	});
	buttons[0].setClickTicks(10);
	buttons[1].attachClick([]{  //GO
		ButtonActionGo();
	});
	buttons[2].attachClick([]{  //UP
		ButtonActionUp();
	});
	buttons[3].attachClick([]{  //DOWN
		ButtonActionDown();
	});
	buttons[4].attachClick([]{  //LEFT
		ButtonActionLeft();
	});
	buttons[5].attachClick([]{  //RIGHT
		ButtonActionRight();
	});
	buttons[6].attachClick([]{  //OK
		ButtonActionOk();
	});
	buttons[6].attachLongPressStart([]{  //OK HOLD
		ButtonActionLongOk();
	});
}

void OTAProgress(uint16_t progress)
{
	if (progress == 1)
	{
		display.ssd1306_command(SSD1306_DISPLAYON);
	}
	if (progress % 5 != 0)
	{
		return;
	}
	
	display.clearDisplay();
	display.setTextSize(1);
	display.setCursor(32, 1);
	display.println("OTA updating");
	display.setCursor(32, 24);
	display.printf("Progress: %d%%", progress);
	display.display();
}

//go into deep sleep only can wake up by charge
void LowBatteryAction()
{
	//power off oled
	display.ssd1306_command(SSD1306_DISPLAYOFF);

	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
	rtc_gpio_isolate(GPIO_NUM_2);	//pull down inside and pull up as I2C_SCL outside, avoid power consume

	uint64_t mask = 0;
	mask |=  1ull << USB_DET;
	esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);	//can wake when rtc shutdown

	esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);	//force shutdown rtc power

	esp_deep_sleep_start();
}

String GetDeviceInfoString()
{
	String result = "Device upCount: " + String(upCount);
	result += ", Charging: " + String((USB_ACTIVE) ? "yes" : "no");
	result += ", Battery voltage: " + String(batteryVol) + " mV";
	// Display the settings.
	result += ", Remote state: ";
	result += ac.toString().c_str();
	// Display the encoded IR sequence.
	unsigned char* ir_code = ac.getRaw();
	result += ", IR Code: 0x";
	char irHex[8] = {0};
	for (uint8_t i = 0; i < kHitachiAc1StateLength; i++)
	{
		sprintf(irHex, "%02X", ir_code[i]);
		result += String(irHex);
	}
	return result;
}