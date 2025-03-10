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
#include "web.h"
#include "config.h"	//please define your wifi ssid and passwd here, avoid to share it on the Internet

#include <U8g2lib.h>

//#include <ir_Hitachi.h>
#include <ir_Goodweather.h>

#include <vector>
#include <map>

using namespace std;

#define SW_ACTIVE (digitalRead(SW_PIN) == 0)
#define USB_ACTIVE (digitalRead(USB_DET) == 1)

#define BAT_SAMPLE_ON  (digitalWrite(BAT_ADCEN, 1))
#define BAT_SAMPLE_OFF (digitalWrite(BAT_ADCEN, 0))


String configFile = "/config.json";
const char* host = "esp32";
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* apssid      = "esp32";
const char* appassword = "pwd4admin";
uint64_t chipid;
const String dev_name = "IRRemote-V1.0";
const String buildTime = __DATE__ ", " __TIME__;

int64_t btnChkMs = 0;
int displayCnt = 20;
int saveDelay = 0;

const uint16_t lowestBatVol = 3450;	//protect battery voltage

static std::map<uint32_t, uint32_t> batVolIconMap = 
{
	//<voltage, index>
	{4050, 5},
	{3980, 4},
	{3850, 3},
	{3780, 2},
	{3620, 1},
	{0, 0,  }
};

U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, DIS_SCL, DIS_SDA);

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
//IRHitachiAc1 ac(kIrLedPin);
IRGoodweatherAc ac(kIrLedPin);

WiFiServer server(80);

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
uint32_t actCount = 0;
uint16_t oledColor = 0;

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
	uint16_t color = 0;
	if (actCount % 10 >= 5)
	{
		color = 1;
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
	}

	auto pJson = doc.getOrAddMember("ac").as<JsonObject>();
	pJson["mode"].set<int>(ac.getMode());
	pJson["fan"].set<int>(ac.getFan());
	pJson["temp"].set<int>(ac.getTemp());
	doc["actCount"] = ++actCount;

	File fileW = SPIFFS.open(configFile, FILE_WRITE);
	serializeJson(doc, fileW);
	fileW.close();
}

String AcModeString(uint8_t mode)
{
	String result = "UNKNOW";
	switch (mode)
	{
	case kGoodweatherCool:
		result = "Cool";
		break;
	case kGoodweatherHeat:
		result = "Heat";
		break;
	case kGoodweatherDry:
		result = "Dry";
		break;
	case kGoodweatherFan:
		result = "Fan";
		break;
	case kGoodweatherAuto:
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
	case kGoodweatherFanHigh:
		result = "High";
		break;
	case kGoodweatherFanMed:
		result = "Med";
		break;
	case kGoodweatherFanLow:
		result = "Low";
		break;
	case kGoodweatherFanAuto:
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

void GetSpiffsFile(String fileName, WiFiClient& client)
{
  String message = "";
  String path = fileName;
  Serial.print("request to: " + path + "\n");
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {
    if(SPIFFS.exists(pathWithGz))
	{
      path += ".gz";
    }
	String contentType = GetContentType(path);

	File file = SPIFFS.open(path, FILE_READ);
	//String data = file.readString();
	client.println("HTTP/1.1 200 OK");
	client.println("Content-Type: " + contentType);
	client.println("Connection: close");
	client.println();
	client.write(file);

	file.close();
	return;
  }
  client.println("HTTP/1.1 404 NOT FOUND");
  client.println("Connection: close");
}

bool ConnectWiFi(String ssid,String pwd)
{
	Serial.println("Trying WiFi: " + ssid);
	int connectCount = 0;
	WiFi.setHostname(dev_name.c_str());
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

	if (SW_ACTIVE)
	{
		print_wakeup_reason();
	}

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
	ac.setSleep(0);
	ac.setPower(false);
	ac.setSwing(kGoodweatherSwingOff);
	ac.setLight(false);
	ac.setSleep(false);

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

		actCount = doc.getMember("actCount").as<uint32_t>();
		Serial.printf("device actCount: %d\n", actCount);
		file.close();
	}
	else
	{
		ac.setMode(kGoodweatherAuto);
		ac.setTemp(23);
		ac.setFan(kGoodweatherFanLow);
		AcBackup();
	}
	u8g2.begin();
	u8g2.setFont(u8g2_font_ncenB12_tr);	// choose a suitable font
	u8g2.drawStr(18, 20, "AC Remote");	// write something to the internal memory

	//set color
	u8x8_cad_StartTransfer(u8g2.getU8x8());
	u8x8_cad_SendCmd(u8g2.getU8x8(), actCount % 10 >= 5 ? OLED_CMD_COLOR_BLACK : OLED_CMD_COLOR_WHITE);
	u8x8_cad_EndTransfer(u8g2.getU8x8());

	u8g2.sendBuffer();	// transfer internal memory to the display

	if (SW_ACTIVE)
	{
		if(!ConnectWiFi(ssid, password) && SPIFFS.exists(configFile))
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
			Serial.println("Config file not found:" + configFile);
		}
		if(!WiFi.isConnected())
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


uint8_t GetBatteryIndex()
{
	int64_t curSec = millis() / 1000;
	uint8_t index = 0;
	static uint8_t chargeStep;
	static int64_t lastSec = curSec;

	for (auto it = batVolIconMap.rbegin(); it != batVolIconMap.rend(); it++)
	{
		if (batteryVol >= it->first)
		{
			index = it->second;
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
		}
	}

	return index;
}


void printOLED()
{
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_ncenB08_tr);

	//MODE
	u8g2.setCursor(1, 9);
	u8g2.printf("Mode: %s", AcModeString(ac.getMode()).c_str());

	//FAN SPEED
	u8g2.setCursor(1, 30);
	u8g2.printf("Fan: %s", AcFanString(ac.getFan()).c_str());

	uint8_t iconPlace = 95;
	const uint8_t iconDis = 12;

	//battery icon
	{
		u8g2.setFont(u8g2_font_battery19_tn);
		u8g2.setFontDirection(1);
		u8g2.drawGlyph(107, 1, 0x0030 + GetBatteryIndex());
		u8g2.setFontDirection(0);
	}
	//usb icon
	if (USB_ACTIVE)
	{
		u8g2.setFont(u8g2_font_siji_t_6x10);
		u8g2.drawGlyph(iconPlace, 9, 0xe00c); // USB
		iconPlace -= iconDis;
	}
	//wifi icon
	if (WiFi.isConnected())
	{
		u8g2.setFont(u8g2_font_siji_t_6x10);
		u8g2.drawGlyph(iconPlace, 9, 0xe21a); // WIFI
		iconPlace -= iconDis;
	}
	//sleep countdown
	if (idleClock > 0)
	{
		u8g2.setCursor(116, 30);
		u8g2.setFont(u8g2_font_ncenB08_tr);
		u8g2.printf("%02d", sleepClock - idleClock);
	}
	//TEMPERATURE
	u8g2.setFont(u8g2_font_ncenB10_tr);
	u8g2.setCursor(64, 30);
	u8g2.printf("%d", ac.getTemp());
	u8g2.setFont(u8g2_font_cu12_t_symbols);
	u8g2.drawGlyph(80, 30, 0x2103);	// ℃

	u8g2.sendBuffer();
}

void printState()
{
	printOLED();

	// Display the settings.
	Serial.println("Remote state: ");
	Serial.printf("  %s\n", ac.toString().c_str());
	// Display the encoded IR sequence.
	Serial.printf("IR Code: %llu", ac.getRaw());

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
	static uint32_t lostConnect = 0;

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
			if (displayCnt == 20)
			{
				u8g2.display();
			}
			
			if (displayCnt > 0)
			{
				displayCnt--;
				if (displayCnt == 1)
				{
					u8g2.noDisplay();
				}
			}

			if (WiFi.isConnected())
			{
				lostConnect = 0;
			}
			else
			{
				if (lostConnect++ > 180)
				{
					Serial.println("\nWiFi retry...");
					WiFi.reconnect();
					if (lostConnect >= 600)
					{
						ESP.restart();
					}
					
				}
			}
		}
		if (saveDelay > 0)
		{
			saveDelay--;
			if (saveDelay == 0)
			{
				AcBackup();
			}
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
		if (WiFi.isConnected())
		{
			WebHandle();
			//server.handleClient();
			ArduinoOTA.handle();
		}
		if (idleClock != 0 && !WiFi.isConnected())
		{
			Serial.println("\nGoing to restart by SW active!");
			ESP.restart();
		}

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
		u8g2.clearDisplay();
		u8g2.setFont(u8g2_font_ncenB08_tr);
		u8g2.setCursor(28, 18);
		u8g2.println("Going to sleep");
		u8g2.sendBuffer();
		AcBackup();

		Serial.println("Going to sleep now");
		Serial.flush();

		esp_sleep_enable_timer_wakeup(1000 * 1000);	//delay(1000)
		esp_light_sleep_start();

		//power off oled
		u8g2.noDisplay();

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
		displayCnt = 20;
	}
	saveDelay = 30;
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
	static bool swingV = false;
	swingV = !swingV;
	ac.setSwing(swingV ? kGoodweatherSwingSlow : kGoodweatherSwingOff);
	AcCmdSend();
}

void AcFanSpeed()
{
	uint8_t speed = ac.getFan();
	switch (speed)
	{
	case kGoodweatherFanAuto:
		speed = kGoodweatherFanLow;
		break;
	case kGoodweatherFanHigh:
		speed = kGoodweatherFanAuto;
		break;
		case kGoodweatherFanMed:
		speed = kGoodweatherFanHigh;
		break;
		case kGoodweatherFanLow:
		speed = kGoodweatherFanMed;
		break;
	default:
		speed = kGoodweatherFanAuto;
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
	case kGoodweatherDry:
		acMode = kGoodweatherFan;
		break;
	case kGoodweatherFan:
		acMode = kGoodweatherCool;
		break;
		case kGoodweatherCool:
		acMode = kGoodweatherHeat;
		break;
		case kGoodweatherHeat:
		acMode = kGoodweatherAuto;
		break;
		case kGoodweatherAuto:
		acMode = kGoodweatherDry;
		break;
	default:
		acMode = kGoodweatherFan;
		break;
	}

	if (acMode == kGoodweatherFan && ac.getFan() == kGoodweatherFanAuto)//auto fan speed not allow in fan mode
	{
		ac.setFan(kGoodweatherFanLow);
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
		u8g2.display();
		displayCnt = 20;
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
		u8g2.noDisplay();
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
		u8g2.display();
	}
	if (progress % 5 != 0)
	{
		return;
	}
	
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_ncenB08_tr);
	u8g2.setCursor(32, 9);
	u8g2.println("OTA updating");
	u8g2.setCursor(32, 30);
	u8g2.printf("Progress: %d%%", progress);
	u8g2.sendBuffer();
}

//go into deep sleep only can wake up by charge
void LowBatteryAction()
{
	//power off oled
	u8g2.noDisplay();

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
	DynamicJsonDocument doc(1024);
	doc["ActCount"] = actCount;
	doc["Dev"] = dev_name;
	doc["Charging"] = (USB_ACTIVE) ? "yes" : "no";
	doc["Batvoltage"] = String(batteryVol) + " mV";
	doc["AcInfo"] = ac.toString();

	doc["LatestIrCode"] = ac.getRaw();
	doc["Build"] = buildTime;

    unsigned long seconds = millis() / 1000;
    int days = seconds / (24 * 3600);
    seconds = seconds % (24 * 3600);
    int hours = seconds / 3600;
    seconds = seconds % 3600;
    int minutes = seconds / 60;
    seconds = seconds % 60;

    char uptime[20];
    snprintf(uptime, sizeof(uptime), "%dd %02d:%02d:%02d", days, hours, minutes, (int)seconds);
    doc["Uptime"] = uptime;

    String result;
    serializeJson(doc, result);
    return result;
}
