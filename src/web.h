#include "main.h"

String getContentType(String filename);
void handleNotFound();
void handleIndex();
void handleLed();
void scanWifi();
void configWifi();
boolean connectWifi(String ssid,String pwd);
void restartESP();
void getUserDir();
void deleteFS();
void getFS();
void uploadFS();
void WebConfig();