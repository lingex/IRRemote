#include "icons.h"
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRtext.h>
#include <WiFi.h>

extern WiFiServer server;
//extern IRHitachiAc1 ac;
extern IRGoodweatherAc ac;
extern int displayCnt;

extern String GetDeviceInfoString();
extern String acPowerState();
extern void AcSwingVSwitch();
extern void AcPowerToggle();
extern void AcCmdSend();
extern void GetSpiffsFile(String fileName, WiFiClient& client);
extern String AcModeString(uint8_t mode);

// Current time
unsigned long currentTime = 0;
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;


void WebHandle()
{
	//WiFiClient client = server.available();   // Listen for incoming clients
	WiFiClient client = server.accept();   // Listen for incoming clients
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
						uint8_t acPower = ac.getPower();

						if (header.indexOf("GET /status") >= 0)
						{
							client.println("HTTP/1.1 200 OK");
							client.println("Content-Type: application/json");
							client.println("Connection: close");
							client.println();
							client.println("{\"power\":" + String(acPower) + ",\"mode\":" + String(acMode) + ",\"fan\":" + String(acFan) + ",\"temp\":" + String(acTemp) + String(",\"modeStr\":\"") + AcModeString(acMode)+  "\"}");
							break;
						}
						else if (header.indexOf("GET /jquery.min.js") >= 0)
						{
							GetSpiffsFile("/jquery.min.js", client);
							break;
						}
						else if (header.indexOf("GET /favicon.ico") >= 0) {
							GetSpiffsFile("/favicon.ico", client);
							break;
						}

						// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
						// and a content-type so the client knows what's coming, then a blank line:
						client.println("HTTP/1.1 200 OK");
						client.println("Content-type:text/html");
						client.println("Connection: close");
						client.println();
						// Display the HTML web page
						client.println("<!DOCTYPE html><html>");
						client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
						//client.println("<link rel=\"icon\" href=\"data:,\">");
//						client.println("<link  rel='shortcut icon' type='image/png' href=\"data:image/x-icon;base64,");
//						client.println(favicon);
//						client.println("\">");
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
						// client.println("<script src=\"https://ajax.aspnetcdn.com/ajax/jquery/jquery-3.3.1.min.js\"></script>");	//sometimes very slow
						//client.println("<script src=\"https://cdn.staticfile.org/jquery/1.10.2/jquery.min.js\"></script>");	//may block by uBlock
						client.println(String("<script src=\"/jquery.min.js\"></script>"));
						//client.println("<script src=\"/jquery.js\"></script>");
								
						// Web Page
						client.println("</head><body><h1>AC Remote</h1>");
						client.println("<p><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"powerbtn\" " + acPowerState() + "><span class=\"slider\"></span></label></p>");
						client.println("<center>");
						client.println("<fieldset style='width:300px; border-radius: 8px' id=\"fieldsetACMode\" onchange=\"ac()\">");
						client.println("<legend align='center'>Mode</legend>");
						client.println("<input type='radio' name='acMODE' value='" + String(kGoodweatherCool) + "' " + String(acMode == kGoodweatherCool ? "checked" : "") +  "/>Cool");
						client.println("<input type='radio' name='acMODE' value='" + String(kGoodweatherHeat) + "' " + String(acMode == kGoodweatherHeat ? "checked" : "") +  "/>Heat");
						client.println("<input type='radio' name='acMODE' value='" + String(kGoodweatherFan) + "' "  + String(acMode == kGoodweatherFan ? "checked" : "") +  "/>Fan");
						client.println("<input type='radio' name='acMODE' value='" + String(kGoodweatherDry) + "' "  + String(acMode == kGoodweatherDry ? "checked" : "") +  "/>Dry");
						client.println("<input type='radio' name='acMODE' value='" + String(kGoodweatherAuto) + "' " + String(acMode == kGoodweatherAuto ? "checked" : "") +  "/>Auto");
						client.println("</fieldset>");
						client.println("<br>");
						client.println("<fieldset style='width:300px; border-radius: 8px' id=\"fieldsetACFan\" onchange=\"ac()\">");
						client.println("<legend align='center'>Fan</legend>");
						client.println("<input type='radio' name='fanSpeed' value='" + String(kGoodweatherFanLow) + "' "  + String(acFan == kGoodweatherFanLow ? "checked" : "")  + "/>Low");
						client.println("<input type='radio' name='fanSpeed' value='" + String(kGoodweatherFanMed) + "' "  + String(acFan == kGoodweatherFanMed ? "checked" : "")  + "/>Med");
						client.println("<input type='radio' name='fanSpeed' value='" + String(kGoodweatherFanHigh) + "' " + String(acFan == kGoodweatherFanHigh ? "checked" : "")  + "/>High");
						client.println("<input type='radio' name='fanSpeed' value='" + String(kGoodweatherFanAuto) + "' " + String(acFan == kGoodweatherFanAuto ? "checked" : "")  + "/>Auto");
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
						client.println("$.get(\"/?mode=\" + modeVal + \"&fan=\" + fanVal + \"&temp=\" + tempVal + \"&\"); {Connection: close};");
						client.println("}");
						//swing action
						client.println("function swingAction(btnVal) {");
						client.println("var modeSelector = document.querySelector('input[name=\"acMODE\"]:checked');");
						client.println("var fanSelector = document.querySelector('input[name=\"fanSpeed\"]:checked');");
						client.println("var modeVal = modeSelector ? modeSelector.value : 1;");
						client.println("var fanVal = modeSelector ? fanSelector.value : 1;");
						client.println("var tempVal = tempSlider ? tempSlider.value : 1;");
						client.println("console.log('mode:' + modeVal + ', fanSpeed:' + fanVal + ', temp:' + tempVal + ', swing:' + btnVal);");
						client.println("$.get(\"/?mode=\" + modeVal + \"&fan=\" + fanVal + \"&temp=\" + tempVal + \"&swing=\" + btnVal + \"&\"); {Connection: close};");
						client.println("}");
						//power switch action
						client.println("function btnAction(btnVal) {");
						client.println("var modeSelector = document.querySelector('input[name=\"acMODE\"]:checked');");
						client.println("var fanSelector = document.querySelector('input[name=\"fanSpeed\"]:checked');");
						client.println("var modeVal = modeSelector ? modeSelector.value : 1;");
						client.println("var fanVal = modeSelector ? fanSelector.value : 1;");
						client.println("var tempVal = tempSlider ? tempSlider.value : 1;");
						client.println("console.log('mode:' + modeVal + ', fanSpeed:' + fanVal + ', temp:' + tempVal + ', switch:' + btnVal);");
						client.println("$.get(\"/?mode=\" + modeVal + \"&fan=\" + fanVal + \"&temp=\" + tempVal + \"&switch=\" + btnVal + \"&\"); {Connection: close};");
						client.println("}");
						//client.println("console.log(" + GetDeviceInfoString() + ")");
						client.println("</script></body></html>");
						
						//GET /?value=140125&switch=1& HTTP/1.1
						bool valueOk = false;
						displayCnt = 20;
						// Check for each parameter separately
						if(header.indexOf("GET /?mode=") >= 0 && header.indexOf("&fan=") >= 0 && header.indexOf("&temp=") >= 0) {
							int mode_start = header.indexOf("mode=") + 5;
							int mode_end = header.indexOf('&', mode_start);
							int mode = header.substring(mode_start, mode_end).toInt();
							
							int fan_start = header.indexOf("fan=") + 4;
							int fan_end = header.indexOf('&', fan_start);
							int fan = header.substring(fan_start, fan_end).toInt();
						
							int temp_start = header.indexOf("temp=") + 5;
							int temp_end = (header.indexOf('&', temp_start) != -1) ? header.indexOf('&', temp_start) : header.indexOf(' ', temp_start);
							int temp = header.substring(temp_start, temp_end).toInt();
						
							if (temp < 16 || temp > 35) {
								temp = 25;
							}
					
							ac.setMode(mode);
							ac.setFan(fan);
							ac.setTemp(temp);
							valueOk = true;
						}
						if(header.indexOf("&swing=") >= 0) {
							int pos = header.indexOf("&swing=") + 7;
							int val = header.substring(pos, pos + 1).toInt();
							if (val == 1) {
								//swing X
								AcSwingVSwitch();
							} else if (val == 2) {
								//swing Y same function as X right now
								AcSwingVSwitch();
							}
						} else if(header.indexOf("&switch=") >= 0) {
							int pos = header.indexOf("&switch=") + 8;
							int val = header.substring(pos, pos + 1).toInt();
							//AcPowerSwitch(val != 0);
							if (val != ac.getPower()) {
								AcPowerToggle();
							}
						} else if(valueOk) {
							AcCmdSend();
						}
						Serial.println(header);
						// The HTTP response ends with another blank line
						client.println();
						// Break out of the while loop
						break;
					} else { // if you got a newline, then clear currentLine
						currentLine = "";
					}
				} else if (c != '\r') {  // if you got anything else but a carriage return character,
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
}