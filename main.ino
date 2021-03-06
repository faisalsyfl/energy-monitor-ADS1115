#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <ATM90E32.h>

/***** CALIBRATION SETTINGS *****/
/* 
 * 4485 for 60 Hz (North America)
 * 389 for 50 hz (rest of the world)
 */

/* 
 * 0 for 10A (1x)
 * 21 for 100A (2x)
 * 42 for between 100A - 200A (4x)
 */

/* 
 * For meter <= v1.3:
 *    42080 - 9v AC Transformer - Jameco 112336
 *    32428 - 12v AC Transformer - Jameco 167151
 * For meter > v1.4:
 *    37106 - 9v AC Transformer - Jameco 157041
 *    38302 - 9v AC Transformer - Jameco 112336
 *    29462 - 12v AC Transformer - Jameco 167151
 * For Meters > v1.4 purchased after 11/1/2019 and rev.3
 *    7611 - 9v AC Transformer - Jameco 157041
 */

/*
 * 25498 - SCT-013-000 100A/50mA
 * 39473 - SCT-016 120A/40mA
 * 46539 - Magnalab 100A
 */

#if defined ESP8266
const int CS_pin = 16;
/*
  D5/14 - CLK
  D6/12 - MISO
  D7/13 - MOSI
*/
#elif defined ESP32
const int CS_pin = 5;
const int CS_pin_second = 4;
/*
  18 - CLK
  19 - MISO
  23 - MOSI
*/

#else
const int CS_pin = SS; // Use default SS pin for unknown Arduino
const int CS_pin_second = 4;
#endif

ATM90E32 eic_first{};  //initialize the IC class
ATM90E32 eic_second{}; //initialize the IC class

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define RELAY1 2
#define RELAY2 3
#define RELAY3 4
#define RELAY4 5

#define RELAY1_SECOND 2
#define RELAY2_SECOND 3
#define RELAY3_SECOND 4
#define RELAY4_SECOND 5

#define UP 12
#define OK 10
#define DOWN 11
#define BACK 9

// U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/13, /* data=*/11, /* CS=*/10, /* reset=*/8);
// robotdyn Arduino Mega mini
//SCK,MOSI,SS/CS
//10 -> 53
//11 -> 51
//13 -> 52
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 52, 51, 53);

Adafruit_ADS1115 ADC_1(0x48); //Create ADS1115 object

const float SCALEFACTOR = 0.125F; // This is the scale factor for the default +/- 4.096V Range we will use - see ADS1X15 Library
// const float BURDEN_RESISTOR = 45;   // Burden resistor where measurement voltage gets measured.
const float VOLTAGE_MAINS = 220.00; // line voltage
const float COIL_WINDING = 2000;    // ratio of sensor 100:0.05

float MAX_CURRENT_COIL = 0.0707; // Maximum current of sensor rated at  Rms 100 A
float MAX_VOLTAGE_ADC = 15.981;  // Maximum Voltage on burden resistor calculated by burden resistor * maxAmps (33 Ohm * 0.0707 A)

float voltage_adc_1 = 0.0; // The result of applying the scale factor to the raw value
float voltage_adc_2 = 0.0;

float ampere_adc_1 = 0.0; // The result of applying the scale factor to the raw value
float ampere_adc_2 = 0.0;

float power_1 = 0.0;
float power_2 = 0.0;
float power_total = 0.0;

byte mainMenuPage = 1;
byte mainMenuPageOld = 1;
byte mainMenuTotal = 17;

unsigned short LineFreq = 389;
unsigned short PGAGain = 42;
unsigned short VoltageGain = 37106;
unsigned short CurrentGainCT1 = 39473; //SCT-019-000 200/30mA
unsigned short CurrentGainCT2 = 39473; //SCT-019-000 200/30mA
unsigned short CurrentGainCT3 = 39473; //SCT-019-000 200/30mA
unsigned short CurrentGainCT4 = 39473; //SCT-019-000 200/30mA
unsigned short CurrentGainCT5 = 25498; //SCT-013-000 100A/50mA
unsigned short CurrentGainCT6 = 25498; //SCT-013-000 100A/50mA
float CurrentGainCT7 = 45;             //SCT-013-000 100A/50mA
float CurrentGainCT8 = 45;             //SCT-013-000 100A/50mA

// board integrated ATM90E32 CircuitSetup
// CT1   CT2  CT3  CT4  CT5  CT6
// 1R    2S   3T   4P   5R   6S
// 200A  200A 200A 200A 100A 100A

// CT7   CT8 ADS1115
// 7T    8P
// 100A  100A

//Riley Setup
int RC1 = 15, RC2 = 35, RC3 = 80, RC4 = 80;
int RC1_SECOND = 15, RC2_SECOND = 35, RC3_SECOND = 80, RC4_SECOND = 80;

void setup()
{
    Serial.begin(9600);

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Setup");
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.sendBuffer();

    ADC_1.setGain(GAIN_TWO);
    ADC_1.begin();

    pinMode(13, OUTPUT);
    pinMode(UP, INPUT_PULLUP);
    pinMode(DOWN, INPUT_PULLUP);
    pinMode(OK, INPUT_PULLUP);
    pinMode(BACK, INPUT_PULLUP);

    //RILEY

    pinMode(RELAY1, OUTPUT);
    pinMode(RELAY2, OUTPUT);
    pinMode(RELAY3, OUTPUT);
    pinMode(RELAY4, OUTPUT);
    pinMode(RELAY1_SECOND, OUTPUT);
    pinMode(RELAY2_SECOND, OUTPUT);
    pinMode(RELAY3_SECOND, OUTPUT);
    pinMode(RELAY4_SECOND, OUTPUT);

    delay(500);
}

void loop()
{
    /*Initialise the ATM90E32 & Pass CS pin and calibrations to its library
    the 2nd (B) current channel is not used with the split phase meter */
    Serial.println("Start ATM90E32");
    eic_first.begin(CS_pin, LineFreq, PGAGain, VoltageGain, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3);
    eic_second.begin(CS_pin_second, LineFreq, PGAGain, VoltageGain, CurrentGainCT4, CurrentGainCT5, CurrentGainCT6);

    char key;
    boolean stat = true;

    key = getPressedKey();

    delay(100);

    if (key == 'U')
    {
        mainMenuPage++;
        if (mainMenuPage > mainMenuTotal)
            mainMenuPage = 1;
    }
    else if (key == 'D')
    {
        mainMenuPage--;
        if (mainMenuPage == 0)
            mainMenuPage = mainMenuTotal;
    }
    if (key != mainMenuPageOld)
    {
        MainMenuDisplay();
        mainMenuPageOld = mainMenuPage;
    }

    if (key == 'O') //enter selected menu
    {
        switch (mainMenuPage)
        {
        case 1:
            MenuAmpere();
            break;
        case 2:
            CurrentGainCT1 = MenuSet(CurrentGainCT1, "CTR");
            break;
        case 3:
            CurrentGainCT2 = MenuSet(CurrentGainCT2, "CTS");
            break;
        case 4:
            CurrentGainCT3 = MenuSet(CurrentGainCT3, "CTT");
            break;
        case 5:
            CurrentGainCT4 = MenuSet(CurrentGainCT4, "CTP");
            break;
        case 6:
            RC1 = MenuSet(RC1, "Relay 1");
            break;
        case 7:
            RC2 = MenuSet(RC2, "Relay 2");
            break;
        case 8:
            RC3 = MenuSet(RC3, "Relay 3");
            break;
        case 9:
            RC4 = MenuSet(RC4, "Relay 4");
            break;
        case 10:
            CurrentGainCT5 = MenuSet(CurrentGainCT5, "SECOND CTR");
            break;
        case 11:
            CurrentGainCT6 = MenuSet(CurrentGainCT6, "SECOND CTS");
            break;
        case 12:
            CurrentGainCT7 = MenuSet(CurrentGainCT7, "SECOND CTT");
            break;
        case 13:
            CurrentGainCT8 = MenuSet(CurrentGainCT8, "SECOND CTP");
            break;
        case 14:
            RC1_SECOND = MenuSet(RC1_SECOND, "SECOND Relay 1");
            break;
        case 15:
            RC2_SECOND = MenuSet(RC2_SECOND, "SECOND Relay 2");
            break;
        case 16:
            RC3_SECOND = MenuSet(RC3_SECOND, "SECOND Relay 3");
            break;
        case 17:
            RC4_SECOND = MenuSet(RC4_SECOND, "SECOND Relay 4 ");
            break;
        }
    }
}

void MenuAmpere()
{
    Serial.println("Menu Ampere");

    //Get From ATM90E32
    /*Repeatedly fetch some values from the ATM90E32 */
    float voltageA_first, voltageB_first, voltageC_first;
    float voltageA_second, voltageB_second, voltageC_second;
    float totalVoltage;
    float currentCT1, currentCT2, currentCT3;
    float currentCT4, currentCT5, currentCT6;
    float totalCurrent, realPower, powerFactor, temp, freq, totalWatts;

    unsigned short sys0 = eic_first.GetSysStatus0();  //EMMState0
    unsigned short sys1 = eic_second.GetSysStatus0(); //EMMState0

    //if true the MCU is not getting data from the energy meter
    if (sys0 == 65535 || sys0 == 0)
        Serial.println("Error: Not receiving data from energy meter 1 - check your connections");
    if (sys1 == 65535 || sys1 == 0)
        Serial.println("Error: Not receiving data from energy meter 2 - check your connections");

    //get voltage
    voltageA_first = eic_first.GetLineVoltageA();
    voltageA_second = eic_second.GetLineVoltageA();

    if (LineFreq = 4485)
    {
        totalVoltage = voltageA + voltageC; //is split single phase, so only 120v per leg
    }
    else
    {
        totalVoltage = voltageA; //voltage should be 220-240 at the AC transformer
    }

    //get current first
    currentCT1 = eic_first.GetLineCurrentA();
    currentCT2 = eic_first.GetLineCurrentB();
    currentCT3 = eic_first.GetLineCurrentC();
    totalCurrent = currentCT1 + currentCT2 + currentCT3;

    //get current second
    currentCT4 = eic_second.GetLineCurrentA();
    currentCT5 = eic_second.GetLineCurrentB();
    currentCT6 = eic_second.GetLineCurrentC();
    totalCurrent = currentCT4 + currentCT5 + currentCT6 + totalCurrent;

    Serial.println("Voltage 1: " + String(voltageA_first) + "V");
    Serial.println("Voltage 2: " + String(voltageA_second) + "V");
    Serial.println("Current 1: " + String(currentCT1) + "A");
    Serial.println("Current 2: " + String(currentCT2) + "A");
    Serial.println("Current 3: " + String(currentCT3) + "A");
    Serial.println("Current 4: " + String(currentCT4) + "A");
    Serial.println("Current 5: " + String(currentCT5) + "A");
    Serial.println("Current 6: " + String(currentCT6) + "A");

    Serial.println("Real Power 1: " + String(voltageA) + "V");
    realPower = eic_first.GetTotalActivePower();
    powerFactor = eic_first.GetTotalPowerFactor();

    Serial.println("Active Power: " + String(realPower) + "W");
    Serial.println("Power Factor: " + String(powerFactor));

    //Get From Ampere ADS1115
    ampere_adc_1 = calcVrms(100, 1);
    ampere_adc_2 = calcVrms(100, 2);
    voltage_adc_1 = (calcVrms(32, 1)) / 1000.0;
    voltage_adc_2 = (calcVrms(32, 2)) / 1000.0;
    power_1 = calcPower(voltage_adc_1, CurrentGainCT7);
    power_2 = calcPower(voltage_adc_2, CurrentGainCT8);
    power_total = power_1 + power_2;

    Serial.println("Current 7: " + String(power_1) + "A");
    Serial.println("Current 8: " + String(power_2) + "A");
    Serial.println();
    Serial.println();
}

double calcVrms(unsigned int Samples, unsigned int phase)
{
    float voltage_rms;
    float squared_voltage;
    float sample_voltage_sum;
    float sample_voltage;

    for (unsigned int n = 0; n < Samples; n++)
    {
        switch (phase)
        {
        case 1:
        {
            sample_voltage = ADC_1.readADC_Differential_0_1();
            break;
        }
        case 2:
        {
            sample_voltage = ADC_1.readADC_Differential_2_3();
            break;
        }
        default:
            break;
        }
        squared_voltage = sample_voltage * sample_voltage;
        sample_voltage_sum += squared_voltage;
    }

    voltage_rms = (sqrt(sample_voltage_sum / Samples) * SCALEFACTOR) * sqrt(2);
    sample_voltage_sum = 0;
    return voltage_rms;
}

double calcPower(float voltage, int calibration)
{
    double power;
    power = voltage / calibration * COIL_WINDING * VOLTAGE_MAINS;
    return power;
}

float MenuSet(float value, String name)
{
    char key;
    while (key != 'B')
    {
        key = getPressedKey();
        Serial.println(name);
        Serial.println(value);
        // lcd.setCursor(0, 1);
        // lcd.print(name);
        if (key == 'D')
        {
            value = value - 0.5;
            if (value < 0)
            {
                value = 0;
            }
            // lcd.setCursor(0, 1);
            // lcd.print("                ");
            // lcd.setCursor(0, 1);
            Serial.println(name);
            Serial.println(value);
        }
        else if (key == 'U')
        {
            value = value + 0.5;
            Serial.println(name);
            Serial.println(value);
            // lcd.setCursor(0, 1);
            // lcd.print("                ");
            // lcd.setCursor(0, 1);
            // Serial.println(name);
            // lcd.print(name);
        }
    }
    return value;
}

void MainMenuDisplay()
{
    //lcd.clear();
    //lcd.setCursor(0, 0);
    switch (mainMenuPage)
    {
    case 1:
        Serial.println("1.Monitor Arus");
        break;
    case 2:
        Serial.println("2.Kalibrasi CT R");
        break;
    case 3:
        Serial.println("3.Kalibrasi CT S");
        break;
    case 4:
        Serial.println("4.Kalibrasi CT T");
        break;
    case 5:
        Serial.println("5.Kalibrasi CT P");
        break;
    case 6:
        Serial.println("6.Set Relay C1");
        break;
    case 7:
        Serial.println("7.Set Relay C2");
        break;
    case 8:
        Serial.println("8.Set Relay C3");
        break;
    case 9:
        Serial.println("9.Set Relay C4");
        break;
    case 10:
        Serial.println("10.SECOND Kalibrasi CT R");
        break;
    case 11:
        Serial.println("11.SECOND Kalibrasi CT S");
        break;
    case 12:
        Serial.println("12.SECOND Kalibrasi CT T");
        break;
    case 13:
        Serial.println("13.SECOND Kalibrasi CT P");
        break;
    case 14:
        Serial.println("14.SECOND Set Relay C1");
        break;
    case 15:
        Serial.println("15.SECOND Set Relay C2");
        break;
    case 16:
        Serial.println("16.SECOND Set Relay C3");
        break;
    case 17:
        Serial.println("17.SECOND Set Relay C4");
        break;
    }
}

char getPressedKey()
{
    char key;
    if (digitalRead(UP) == 0)
    {
        key = 'U';
    }
    if (digitalRead(DOWN) == 0)
    {
        key = 'D';
    }
    if (digitalRead(OK) == 0)
    {
        key = 'O';
    }
    if (digitalRead(BACK) == 0)
    {
        key = 'B';
    }
    return key;
}