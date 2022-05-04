#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <string>
#include <WebServer.h>
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

#include "main.h"
#include "ota.h"
#include "web.h"
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

String configFile = "/config.ini";
String acConfig = "/ac.ini";

// 默认机器名字
const char* host = "esp32";

const char* ssid = "your ssid";
const char* password = "your passwd";

const char* apssid      = "esp32";
const char* appassword = "pwd4admin";

// esp32 读取信息
uint64_t chipid;

int64_t btnChkMs = 0;

static std::map<uint32_t, std::pair<uint32_t, const unsigned char *>> batVolIconMap = 
{
	//<index, <voltage, icon>>
	{6, {4050, bat6_icon8x8}},
	{5, {3950, bat5_icon8x8}},
	{4, {3880, bat4_icon8x8}},
	{3, {3760, bat3_icon8x8}},
	{2, {3700, bat2_icon8x8}},
	{1, {3620, bat1_icon8x8}},
	{0, {0,    bat0_icon8x8}}
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

WebServer server(80);

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

RTC_DATA_ATTR uint8_t bkpInit = 0;
RTC_DATA_ATTR uint8_t acMode = 0;
RTC_DATA_ATTR uint8_t acFan = 0;
RTC_DATA_ATTR uint8_t acTemp = 0;
RTC_DATA_ATTR uint16_t wakeupCount = 0;
RTC_DATA_ATTR uint64_t uptime = 0;  //in seconds
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

uint16_t GetOledColor()
{
	uint16_t color = SSD1306_WHITE;
	if (wakeupCount % 10 >= 5)
	{
		color = SSD1306_BLACK;
	}
	return color;
}

void AcBackup()
{
	Serial.println("AC state backup.");
	acMode = ac.getMode();
	acFan = ac.getFan();
	acTemp = ac.getTemp();
	bkpInit = 0xc5;

	//if(SPIFFS.exists(acConfig))
	{
		File file = SPIFFS.open(acConfig, "w");
		String data = file.readString();
		DynamicJsonDocument doc(512);
		deserializeJson(doc, data);
		doc["mode"].set<int>(acMode);
		doc["fan"].set<int>(acFan);
		doc["temp"].set<int>(acTemp);

		serializeJson(doc, file);
		file.close();
	}
}

void AcRecovery()
{
	Serial.println("AC state restore.");
	ac.setMode(acMode);
	ac.setFan(acFan);
	ac.setTemp(acTemp);
}

String AcModeString(uint8_t mode)
{
  String result = "";
  result.reserve(22);  // ", Mode: NNN (UNKNOWN)"
  result += irutils::addIntToString(mode, kModeStr, false);
  result += kSpaceLBraceStr;
  if (mode == kHitachiAc1Auto) result += kAutoStr;
    else if (mode == kHitachiAc1Cool) result += kCoolStr;
    else if (mode == kHitachiAc1Heat) result += kHeatStr;
    else if (mode == kHitachiAc1Dry)  result += kDryStr;
    else if (mode == kHitachiAc1Fan)  result += kFanStr;
    else
      result += kUnknownStr;
    return result + ')';
}

String AcFanString(uint8_t speed)
{
	String result = "";
    result.reserve(21);  // ", Fan: NNN (UNKNOWN)"
    result += irutils::addIntToString(speed, kFanStr, false);
    result += kSpaceLBraceStr;
    if (speed == kHitachiAc1FanHigh)           result += kHighStr;
    else if (speed == kHitachiAc1FanLow)       result += kLowStr;
    else if (speed == kHitachiAc1FanAuto) result += kAutoStr;
    //else if (speed == kHitachiAc1FanLow)     result += kQuietStr;
    else if (speed == kHitachiAc1FanMed)    result += kMediumStr;
    //else if (speed == kHitachiAc1FanHigh)   result += kMaximumStr;
    else
      result += kUnknownStr;
    return result + ')';
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

void setup()
{
	oledColor = GetOledColor();

	pinMode(LED, OUTPUT);
	digitalWrite(LED, 1);

	// Setup serial interface
	Serial.begin(115200);
	delay(10);

	print_wakeup_reason();
	wakeupCount++;

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

	Wire.setPins(DIS_SDA, DIS_SCL);
	if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
	{
		Serial.println(F("SSD1306 allocation failed"));
		//for(;;); // Don't proceed, loop forever
	}
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

	SPIFFS.begin();

	if (SW_ACTIVE)
	{
		//连接状态 0未连接 1已连接
		int connect_status = 0;
		// 读取配置文件
		if(SPIFFS.exists(configFile))
		{
			File file = SPIFFS.open(configFile, "r");
			String data = file.readString();
			Serial.print("configFile:");
			Serial.println(data);

			DynamicJsonDocument doc(1024);
			deserializeJson(doc, data);
			JsonArray arr = doc.as<JsonArray>();
			Serial.print("JsonSize:");
			Serial.println(arr.size());
			for(unsigned int i=0;i<arr.size();i++)
			{
				if(connectWifi(arr[i]["ssid"],arr[i]["pwd"]))
				{
					connect_status = 1;
					break;
				}
			}
			//最后关闭文件
			file.close();
		}
		else
		{
			Serial.println("File not found:" + configFile);
		}

		//未连接wifi的时候使用内置ssid和密码尝试连接，如果仍无法连接到wifi则开启AP
		if(connect_status==0)
		{
			Serial.println("使用内置连接尝试");
			if(connectWifi(ssid, password))
			{
				connect_status = 1;
			}
			else
			{
				Serial.println("使用内置连接失败,开启AP等待用户连接");
				Serial.println("SSID:"+String(apssid));
				Serial.println("PASSWORD:"+String(appassword));
				WiFi.mode(WIFI_AP);
				//WIFI_AP_STA模式不稳定，不建议使用
				//WiFi.mode(WIFI_AP_STA);
				delay(500);
				/*
				ssid:热点名,最大63个英文字符
				password:可选参数,可以没有密码
				channel:信道1-13,默认1
				hidden:是否隐藏,true是隐藏
				WiFi.softAP(ssid, password, channel, hidden);

				配置网关等
				IPAddress local_IP(192,168,4,4);
				IPAddress gateway(192,168,4,1);
				IPAddress subnet(255,255,255,0);
				WiFi.softAPConfig(local_IP, gateway, subnet);
				*/
				boolean ap_status = WiFi.softAP(apssid, appassword);
				if(ap_status){
				Serial.println("AP Ready!!");
				IPAddress myIP = WiFi.softAPIP();
				Serial.print("AP IP : ");
				Serial.println(myIP);
				}
				else
				{
					Serial.println("Fail!!");
				}
			}
		}

		WebConfig();
		openOTA();

		/*use mdns for host name resolution*/
		if (!MDNS.begin(host)) { //http://esp32.local
			Serial.println("Error setting up MDNS responder!");
			while (1) {
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

	//Serial.printf("AC model:%s.\n", "R_LT0541_HTA_B");
	ac.begin();
	ac.setModel(R_LT0541_HTA_B);
	ac.setSleep(0);
	ac.setPower(false);
	ac.setPowerToggle(false);
	ac.setSwingToggle(false);
	ac.setSwingH(false);
	ac.setSwingV(false);
	if (bkpInit == 0xc5)
	{
		AcRecovery();
	}
	else
	{
		if(SPIFFS.exists(acConfig))
		{
			File file = SPIFFS.open(acConfig, "r");
			String data = file.readString();
			DynamicJsonDocument doc(512);
			deserializeJson(doc, data);
			acMode = doc["mode"].as<int>();
			acFan = doc["fan"].as<int>();
			acTemp = doc["temp"].as<int>();
		}
		if (acMode == 0 || acFan == 0 || acTemp == 0)
		{
			acMode = kHitachiAc1Cool;
			acFan = kHitachiAc1FanLow;
			acTemp = kHitachiAc1TempAuto;
		}
		if(!SPIFFS.exists(acConfig))
		{
			AcBackup();
		}
		ac.setMode(acMode);
		ac.setTemp(acTemp);
		ac.setFan(acFan);
		ac.setSwingV(false);
	}
	printState();
	digitalWrite(LED, 0);
}

const unsigned char * GetBatIcon()
{
	const unsigned char * pIcon = bat0_icon8x8;
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
	// text display tests
	display.setTextSize(1);
	display.setCursor(1, 1);
	display.printf("%s", AcModeString(ac.getMode()).c_str());
	display.setCursor(1, 12);
	display.printf("Temp: %dC", ac.getTemp());
	
	//display.setCursor(1, 24);
	//display.printf("Swing: %s", ac.getSwingToggle() ? "on":"off");
	display.setCursor(1, 24);
	display.printf("%s", AcFanString(ac.getFan()).c_str());

	uint8_t iconPlace = 120;
	const uint8_t iconDis = 10;

	//battery icon
	{
	#if 0
		display.setCursor(96, 12);
		display.printf("%0.2fV", batteryVol / 1000.0);
	#endif
		display.drawBitmap(iconPlace, 1, GetBatIcon(), 8, 8, SSD1306_WHITE);
		iconPlace -= iconDis;
	}
	//usb icon
	if (USB_ACTIVE)
	{
		//display.drawBitmap(120, 1, bat5_icon8x8, 8, 8, SSD1306_WHITE);	//charge icon

		//display.setCursor(106, 1);
		//display.print("USB");
		display.drawBitmap(iconPlace, 1, usb_icon8x8, 8, 8, SSD1306_WHITE);
		iconPlace -= iconDis;
	}
	//wifi icon
	if (WiFi.isConnected())
	{
		//display.setCursor(72, 12);
		//display.print("WIFI");
		display.drawBitmap(iconPlace, 1, wifi_icon8x8, 8, 8, SSD1306_WHITE);
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
	Serial.println("Hitachi A/C remote is in the following state:");
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
		uptime++;
		lastRefresh = curMs;
		batteryVol = GetBatteryVol();
		//Serial.printf("batVal: %dmV\n", batteryVol);
		if (!SW_ACTIVE)
		{
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
		if (idleClock != 0 && !WiFi.isConnected())
		{
			Serial.println("\nGoing to restart by SW active!");
			ESP.restart();
		}
		
		server.handleClient();
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
	uint8_t mode = ac.getMode();
	switch (mode)
	{
	case kHitachiAc1Dry:
		mode = kHitachiAc1Fan;
		break;
	case kHitachiAc1Fan:
		mode = kHitachiAc1Cool;
		break;
		case kHitachiAc1Cool:
		mode = kHitachiAc1Heat;
		break;
		case kHitachiAc1Heat:
		mode = kHitachiAc1Auto;
		break;
		case kHitachiAc1Auto:
		mode = kHitachiAc1Dry;
		break;
	default:
		mode = kHitachiAc1Fan;
		break;
	}
	ac.setMode(mode);
	AcCmdSend();
}

void ButtonActionGo()
{
	idleClock = 0;
	Serial.println("BTN GO CLICK");
	Serial.printf("Uptime: %dd %02d:%02d:%02d\n\n", (int)(uptime / 86400), (int)(uptime % 86400 / 3600), (int)(uptime % 86400 % 3600 / 60)
		, (int)(uptime % 86400 % 3600 % 60 % 60));
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
