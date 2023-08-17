#ifndef __MAIN_H__
#define __MAIN_H__

#include <Arduino.h>

/* ------------------------- CUSTOM GPIO PIN MAPPING ------------------------- */


#define IR_IN 16
#define IR_OUT 5
#define BAT_ADC 33
#define BAT_ADCEN 12
#define SWITCH_MODE 27
#define KEY_BOOT 0
#define KEY_GO 34
#define KEY_UP 25
#define KEY_DOWN 32
#define KEY_LEFT 26
#define KEY_RIGHT 35
#define KEY_OK 13
#define SW_PIN 27
#define USB_DET 14
#define DIS_SCL 2
#define DIS_SDA 15
#define LED 4


#define IR_CODE_PWR 	0x40BD28D7
#define IR_CODE_BACK 	0x40BDA857
#define IR_CODE_UP 		0x40BD48B7
#define IR_CODE_LEFT 	0x40BD8877
#define IR_CODE_OK 		0x40BD12ED
#define IR_CODE_RIGHT 	0x40BD08F7
#define IR_CODE_DOWN 	0x40BDC837
#define IR_CODE_MODE 	0x40BDE817
#define IR_CODE_MENU 	0x40BD32CD


#define OLED_CMD_COLOR_WHITE 0xA6
#define OLED_CMD_COLOR_BLACK 0xA7


void AcPowerSwitch(bool on);
void OTAProgress(uint16_t progress);

#endif
