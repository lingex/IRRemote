#ifndef __MAIN_H__
#define __MAIN_H__

#include <Arduino.h>

/* ------------------------- CUSTOM GPIO PIN MAPPING ------------------------- */


#define IR_OUT 21
#define BAT_ADC 34
#define BAT_ADCEN 14
#define KEY_BOOT 0
#define KEY_GO 13
#define KEY_UP 33
#define KEY_DOWN 35
#define KEY_LEFT 32
#define KEY_RIGHT 27
#define KEY_OK 15
#define SW_PIN 26
#define USB_DET 25
#define DIS_BL 23
#define DIS_SCK 19
#define DIS_SDA 18
#define DIS_DC 5
#define DIS_RST 17
#define DIS_CS 16
#define LED 4

void AcPowerSwitch(bool on);
void OTAProgress(uint16_t progress);

#endif
