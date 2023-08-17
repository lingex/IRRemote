#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <string>
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRtext.h>
#include "WiFiManager.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <OneButton.h>

#include "main.h"
#include "ota.h"
#include "rtc_io.h"
#include "web.h"

#include <U8g2lib.h>

#include <ir_Hitachi.h>

#include <vector>
#include <map>

using namespace std;

#define SW_ACTIVE (digitalRead(SW_PIN) == 0)
#define USB_ACTIVE (digitalRead(USB_DET) == 1)
#define OK_ACTIVE (digitalRead(KEY_OK) == 1)

#define BAT_SAMPLE_ON  (digitalWrite(BAT_ADCEN, 1))
#define BAT_SAMPLE_OFF (digitalWrite(BAT_ADCEN, 0))

#define DIS_BL_ON  (digitalWrite(DIS_BL, 0))
#define DIS_BL_OFF (digitalWrite(DIS_BL, 1))


String configFile = "/config.json";
const char* host = "esp32";
const char* ssid = "your ssid";
const char* password = "your passwd";
const char* apssid      = "esp32";
const char* appassword = "pwd4admin";
uint64_t chipid;

int64_t btnChkMs = 0;

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

U8G2_UC1701_MINI12864_F_4W_SW_SPI u8g2(U8G2_R0, DIS_SCK, DIS_SDA, DIS_CS, DIS_DC, DIS_RST);

// GPIO to use to control the IR LED circuit. Recommended: 4 (D2).
const uint16_t kIrLedPin = IR_OUT;

// The IR transmitter.
IRHitachiAc1 ac(kIrLedPin);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 8 * 3600, 300 * 1000);
WiFiServer server(80);

std::vector<OneButton> buttons = {
	OneButton(KEY_BOOT, true, false),
	OneButton(KEY_GO, false, false),
	OneButton(KEY_UP, false, false),
	OneButton(KEY_DOWN, false, false),
	OneButton(KEY_LEFT, false, false),
	OneButton(KEY_RIGHT, false, false),
	OneButton(KEY_OK, false, false)
};

uint32_t batteryVol = 0;	//battary voltage in mV
uint16_t idleClock = 0;
const uint16_t sleepClock = 15;

//RTC_DATA_ATTR uint8_t acMode = 0;
uint32_t upCount = 0;

void handleModeInt();
void handleOkInt();
void handleUpInt();
void handleDownInt();
void handleLeftInt();
void handleRightInt();

void ButtonEventsAttach();
void printState();
void printLCD();
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

	if (SW_ACTIVE)
	{
		print_wakeup_reason();
	}

	/*After waking up from sleep, the IO pad used for wakeup will be configured as RTC IO. 
	Therefore, before using this pad as digital GPIO, 
	users need to reconfigure it using rtc_gpio_deinit() function.
	*/
	rtc_gpio_deinit((gpio_num_t)KEY_BOOT);
	rtc_gpio_deinit((gpio_num_t)KEY_OK);
	rtc_gpio_deinit((gpio_num_t)SW_PIN);
	rtc_gpio_deinit((gpio_num_t)KEY_GO);
	rtc_gpio_deinit((gpio_num_t)KEY_UP);
	rtc_gpio_deinit((gpio_num_t)KEY_DOWN);
	rtc_gpio_deinit((gpio_num_t)KEY_LEFT);
	rtc_gpio_deinit((gpio_num_t)KEY_RIGHT);
	gpio_hold_dis((gpio_num_t)DIS_RST);

	pinMode(SW_PIN, INPUT);
	pinMode(USB_DET, INPUT);
	pinMode(BAT_ADCEN, OUTPUT);
	pinMode(DIS_BL, OUTPUT);
	BAT_SAMPLE_OFF;


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

		upCount = doc.getMember("upCount").as<uint32_t>();
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

	u8g2.begin();
	u8g2.setFont(u8g2_font_ncenB12_tr);	// choose a suitable font
	u8g2.drawStr(18, 20, "AC Remote");	// write something to the internal memory

	u8g2.sendBuffer();	// transfer internal memory to the display

	DIS_BL_ON;

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

		timeClient.begin();
		timeClient.update();
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


void printLCD()
{
	u8g2.clearBuffer();

	//POWER STATE
	u8g2.setFont(u8g2_font_ncenB08_tr);
	u8g2.setCursor(0, 9);
	u8g2.printf("%s", ac.getPower() ? "ON" : "OFF");

	//MODE
	u8g2.setCursor(0, 24);
	u8g2.printf("Mode: %s", AcModeString(ac.getMode()).c_str());

	//lines
	u8g2.drawLine(0, 10, 127, 10);
	u8g2.drawLine(67, 10, 67, 63);

	//FAN SPEED
	u8g2.setCursor(0, 38);
	u8g2.printf("Fan: %s", AcFanString(ac.getFan()).c_str());

	//BAT VOLTAGE
	//u8g2.setCursor(1, 48);
	//u8g2.printf("Bat: %dmV", batteryVol);

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
		u8g2.setCursor(116, 63);
		u8g2.setFont(u8g2_font_helvR08_tr);
		u8g2.printf("%02d", sleepClock - idleClock);
	}
	//TEMPERATURE
	u8g2.setFont(u8g2_font_fur30_tr);
	u8g2.setCursor(68, 44);
	u8g2.printf("%d", ac.getTemp());
	u8g2.setFont(u8g2_font_cu12_t_symbols);
	u8g2.drawGlyph(112, 44, 0x2103);	// ℃

	//ntp time
	if (SW_ACTIVE)
	{
		u8g2.setCursor(0, 63);
		u8g2.setFont(u8g2_font_helvR08_tr);
		u8g2.printf("%02d:%02d:%02d", timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
	}

	u8g2.sendBuffer();
}

void printState()
{
	printLCD();

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
	delay(6);
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
			if (batteryVol < lowestBatVol && !USB_ACTIVE)
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
		printLCD();
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
		WebHandle();
		if (idleClock != 0 && !WiFi.isConnected())
		{
			Serial.println("\nGoing to restart by SW active!");
			ESP.restart();
		}
		//server.handleClient();
		ArduinoOTA.handle();
		timeClient.update();
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
		DIS_BL_OFF;

		//wait until button release
		while (OK_ACTIVE)
		{
			esp_sleep_enable_timer_wakeup(100 * 1000);	//wakeup every 100 millisec
			esp_light_sleep_start();
			yield();
		}

		gpio_hold_en((gpio_num_t)DIS_RST);//cost about 200uA if not doing this
		gpio_deep_sleep_hold_en();

		//esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY_BOOT, 0);	//can wake only when rtc active
		esp_sleep_enable_ext0_wakeup((gpio_num_t)SW_PIN, 0);	//can wake only when rtc active

		uint64_t mask = 0;
		mask |= ((1ull << KEY_OK) | (1ull << KEY_GO) | (1ull << KEY_RIGHT) | (1ull << KEY_LEFT) | (1ull << KEY_UP) | (1ull << KEY_DOWN));
		esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);	//can wake when rtc shutdown

		esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
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
	printState();
	digitalWrite(LED, 1);
	ac.send();
	digitalWrite(LED, 0);
	//printState();
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
		//u8g2.display();
		DIS_BL_ON;
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
		//u8g2.noDisplay();
		DIS_BL_OFF;
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
	u8g2.setCursor(30, 20);
	u8g2.println("OTA updating");
	u8g2.setCursor(30, 40);
	u8g2.printf("Progress: %d%%", progress);
	u8g2.sendBuffer();
}

//go into deep sleep only can wake up by charge
void LowBatteryAction()
{
	//power off oled
	u8g2.noDisplay();

	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

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