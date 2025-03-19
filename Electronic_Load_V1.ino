#include <LiquidCrystal.h>
#include <SPI.h>
#include <SD.h>

#define DEBUG 1
#define BAUDRATE_SERIAL0 115200
#define Kp 5.0
#define Ki 0.2
#define Kd 1.0
#define MaxSystemVoltage 55
#define MaxSystemCurrent 10

//---------------- PIN DECLARATIONS---------------------
//LCD pin to Arduino, using 4-bit mode
const int pin_RS = 8;
const int pin_EN = 9;
const int pin_d4 = 4;   // Data Line 4
const int pin_d5 = 5;   // Data Line 5
const int pin_d6 = 6;   // Data Line 6
const int pin_d7 = 7;   // Data Line 7
const int pin_BL = 10;  // LCD Backlight
// Function Pins
const int SDchipSelect = 2;
const int pwmPin = 3;  // Change this to your desired PWM pin
const int btnPin = A0;
const int currentSensorPin = A1;  // ACS712 sensor pin
const int voltageSensePin = A2;   // Voltage estimated via A2 in future will use adc ads1115 of more precise values
// Menu States
enum MenuState {
  MAIN_MENU,
  SETTINGS_MENU,
  SET_VOLTAGE,
  SET_CURRENT,
  MONITOR_POWER,
  ABOUT
};
MenuState currentState = MAIN_MENU;
// Menu Items
enum {
  Start,
  Settings,
  About
};
enum {
  Set_Voltage,
  Set_Current,
};
// A0 Button
enum ButtonSelect {
  PLACEHOLDER_FOR_0,
  SELECT,
  LEFT,
  DOWN,
  UP,
  RIGHT
};

//---------------- VARIABLE DECLARATIONS---------------------
int raw_A0_value = 0;
// Floats for resistor values in divider (in ohms)
float R1 = 1000000.0;
float R2 = 75000.0;  
// Float for Reference Voltage
float ref_voltage = 5.0;

int target_power = 240;  // targetVoltage * targetCurrent, default 60 * 4 = 240 W
char buffer[50];         // Enough space for the serial0 message
unsigned long lastPowerUpdate = 0;
unsigned long lastVoltageUpdate = 0;
const int powerUpdateInterval = 500;        // 500ms update interval
const int VoltageReadUpdateInterval = 500;  // 500ms update interval
int pwmValue = 0;                           // Initial PWM duty cycle (0-255)
unsigned long lastPressTime = 0;
const int debounceDelay = 200;  // Milliseconds to avoid flicker
int mainMenuIndex = Start;
int settingsMenuIndex = Set_Voltage;
int targetVoltage = 60;  // Default Target Voltage
int targetCurrent = 4;   // Default Target Current
int lastKey = 0;
float measuredVoltage = 0.0;  // Variable to store supply voltage
float measuredCurrent = 0.0;  // Variable to store sensor output
float measuredPower = 0.0;    // Variable to store estimated Power Consumed

//---------------- OBJECT DECLARATIONS---------------------
LiquidCrystal lcd(pin_RS, pin_EN, pin_d4, pin_d5, pin_d6, pin_d7);

void setup() {
  delay(1000);
  Serial.begin(BAUDRATE_SERIAL0);
  Serial.print("Serial Started at Baud: ");
  Serial.println(BAUDRATE_SERIAL0);

  lcd.begin(16, 2);  // col, row
  lcd.clear();

  if (!SD.begin(SDchipSelect)) {
    Serial.println("SD Initialization failed!");
    lcd.setCursor(0, 0);
    lcd.print("SD Card Failed!");
    delay(3000);
  } else {
    debugPrint("SD Card Present");
  }

  lcd.clear();
  lcd.setCursor(0, 0);  // col, row
  lcd.print(" Electronic Load");
  delay(1000);
  lcd.clear();

  updateDisplay();
}

void loop() {
  int key = getKey();
  if (key != lastKey && ((millis() - lastPressTime) > debounceDelay)) {
    lastPressTime = millis();
    lastKey = key;
    handleMenu(key);
    printButtonPress(key);
    updateDisplay();
  }
  if (currentState == ABOUT) {
    displayAbout();  // Continuously update About screen
  } else if (currentState == MONITOR_POWER) {
    monitorPower();
  }

  unsigned long voltageMillis = millis();

  if (voltageMillis - lastVoltageUpdate >= VoltageReadUpdateInterval) {
    lastVoltageUpdate = voltageMillis;
    measureVoltage();
    Serial.print("measuredVoltage: ");
    Serial.println(measuredVoltage);
  }
}

void handleMenu(int key) {
  switch (currentState) {
    case MAIN_MENU:
      if (key == DOWN) mainMenuIndex = (mainMenuIndex + 1) % 3;
      if (key == UP) mainMenuIndex = (mainMenuIndex + 2) % 3;
      if (key == SELECT) {
        if (mainMenuIndex == Settings) currentState = SETTINGS_MENU;    // switch to settings menu
        else if (mainMenuIndex == Start) currentState = MONITOR_POWER;  // Start Loading the Supply
        else if (mainMenuIndex == About) currentState = ABOUT;          // Start Loading the Supply
      }
      break;

    case SETTINGS_MENU:
      if (key == DOWN) settingsMenuIndex = (settingsMenuIndex + 1) % 2;
      if (key == UP) settingsMenuIndex = (settingsMenuIndex + 2) % 2;
      if (key == SELECT) currentState = (settingsMenuIndex == 0) ? SET_VOLTAGE : SET_CURRENT;
      if (key == LEFT) currentState = MAIN_MENU;
      break;

    case SET_VOLTAGE:
      if ((key == UP) && (targetVoltage < MaxSystemVoltage)) {
        targetVoltage++;
        updateTargetPower();
      } else if ((key == UP) && (targetVoltage >= MaxSystemVoltage)) {
        debugPrint("Voltage limit reached!");  // Notify user
      }
      if ((key == DOWN) && (targetVoltage > 0)) {
        targetVoltage--;
        updateTargetPower();
      }
      if (key == LEFT) currentState = SETTINGS_MENU;
      break;

    case SET_CURRENT:
      if ((key == UP) && (targetCurrent < MaxSystemCurrent)) {
        targetCurrent++;
        updateTargetPower();
      } else if ((key == UP) && (targetCurrent >= MaxSystemCurrent)) {
        debugPrint("Current limit reached!");  // Notify user
      }
      if ((key == DOWN) && (targetCurrent > 0)) {
        targetCurrent--;
        updateTargetPower();
      }
      if (key == LEFT) currentState = SETTINGS_MENU;
      break;
    case MONITOR_POWER:
      if (key == LEFT) currentState = MAIN_MENU;  // Exit monitoring
      break;
  }
}

void updateDisplay() {
  static float lastCurrent = -1, lastPower = -1, lastVoltage = -1;
  switch (currentState) {
    case MAIN_MENU:
      lcd.clear();
      lcd.setCursor(0, 0);  // col, row
      lcd.print(">" + getMenuItem(mainMenuIndex));
      lcd.setCursor(0, 1);  // col, row
      lcd.print(" Select to Enter ");
      break;
    case SETTINGS_MENU:
      lcd.clear();
      lcd.setCursor(0, 0);  // col, row
      lcd.print("> " + getSettingsItem(settingsMenuIndex));
      lcd.setCursor(0, 1);  // col, row
      lcd.print(" Left to Go Back ");
      break;
    case SET_VOLTAGE:
      lcd.clear();
      lcd.setCursor(0, 0);  // col, row
      lcd.print("Set Voltage:");
      lcd.setCursor(0, 1);  // col, row
      lcd.print(targetVoltage);
      lcd.print("V");
      break;
    case SET_CURRENT:
      lcd.clear();
      lcd.setCursor(0, 0);  // col, row
      lcd.print("Set Current:");
      lcd.setCursor(0, 1);  // col, row
      lcd.print(targetCurrent);
      lcd.print("A");
      break;
    case MONITOR_POWER:
      lcd.setCursor(0, 0);  // col, row
      lcd.print("Current:");
      if (measuredCurrent != lastCurrent) {
        lcd.setCursor(8, 0);
        lcd.print("        ");  // Clear previous value
        lcd.setCursor(8, 0);
        lcd.print(measuredCurrent, 2);
        lcd.print(" A");
      }
      lcd.setCursor(0, 1);  // col, row
      lcd.print("Power:");
      if (measuredPower != lastPower) {
        lcd.setCursor(6, 1);
        lcd.print("           ");  // Clear previous value
        lcd.setCursor(8, 1);
        lcd.print(measuredPower, 2);
        lcd.print(" W");
      }

      lastCurrent = measuredCurrent;
      lastPower = measuredPower;
      break;
    case ABOUT:
      displayAbout();
      break;
  }
}

String getMenuItem(int index) {
  String menuItems[] = { "Start", "Settings", "About" };
  return menuItems[index];
}

String getSettingsItem(int index) {
  String settingsItems[] = { "Set Voltage", "Set Current" };
  return settingsItems[index];
}

int getKey() {
  int val = analogRead(btnPin);
  if (val < 60) return RIGHT;
  if (val < 200) return UP;
  if (val < 400) return DOWN;
  if (val < 600) return LEFT;
  if (val < 800) return SELECT;
  return 0;  // NO BUTTON
}

void printButtonPress(int key) {
  if (key == PLACEHOLDER_FOR_0) return;  // Ignore invalid key presses

  String message = "Button Pressed: ";

  switch (key) {
    case SELECT: message += "SELECT"; break;
    case LEFT: message += "LEFT"; break;
    case DOWN: message += "DOWN"; break;
    case UP: message += "UP"; break;
    case RIGHT: message += "RIGHT"; break;
  }

  // If inside voltage or current setting, also print values
  if (currentState == MAIN_MENU) {
    message += " | Main Menu: " + getMenuItem(mainMenuIndex);
  } else if (currentState == SETTINGS_MENU) {
    message += " | Settings Menu: " + getSettingsItem(settingsMenuIndex);
  } else if ((currentState == SET_VOLTAGE) || (currentState == SET_CURRENT)) {
    message += " | targetVoltage: " + String(targetVoltage) + "V" + " | targetCurrent: " + String(targetCurrent) + " A" + " | Target Power: " + String(target_power) + " W";
  }

  debugPrint(message);
  logKeys(message);  // Logging Key Strokes
}

float getSmoothReading(int pin, int numSamples = 10) {
  float sum = 0;
  for (int i = 0; i < numSamples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return sum / numSamples;
}

void measureCurrent() {
  int rawValue = getSmoothReading(currentSensorPin);
  float sensorVoltage = rawValue * (5.0 / 1023.0);  // Convert ADC value to voltage
  measuredCurrent = (sensorVoltage - 2.5) / 0.1;    // ACS712-20A: 100mV per A
  if (measuredCurrent < 0) measuredCurrent = 0.0;
}

void measureVoltage() {
  int rawValue = getSmoothReading(voltageSensePin);
  float sensorVoltage = 0.78 * (rawValue * ref_voltage) / 1023.0;  // Convert ADC value to voltage
  measuredVoltage =  sensorVoltage / (R2/(R1+R2));//mapfloat(sensorVoltage, 0.0, 5.0, 0.0, 55.0);
  if (measuredVoltage < 0) measuredVoltage = 0.0;
}

void updateMeasuredPower() {
  String message = "Measured Voltage: ";

  measuredPower = measuredVoltage * measuredCurrent;

  message += String(measuredVoltage) + " V" + " | measuredCurrent: " + String(measuredCurrent) + " A" + " | measuredPower: " + String(measuredPower) + " W" + " | regulationPWMvalue: " + String(pwmValue);

  debugPrint(message);
  logPower(message);  // Logging Key Strokes
}

void updateTargetPower() {
  target_power = targetVoltage * targetCurrent;
}

void monitorPower() {
  if (currentState == MONITOR_POWER) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastPowerUpdate >= powerUpdateInterval) {
      lastPowerUpdate = currentMillis;

      measureCurrent();
      measureVoltage();
      updateMeasuredPower();
      regulateCurrent();  // Adjust PWM to maintain current
      updateDisplay();
    }
  }
}

void regulateCurrent() {
  static float previousError = 0;
  static float integral = 0;
  float error = targetCurrent - measuredCurrent;

  integral += error * 0.1;
  float derivative = (error - previousError) / 0.1;

  pwmValue += (error * Kp) + (integral * Ki) + (derivative * Kd);

  pwmValue = constrain(pwmValue, 0, 255);
  analogWrite(pwmPin, pwmValue);

  previousError = error;
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void debugPrint(String message) {
  if (DEBUG) Serial.println(message);
}

void displayAbout() {
  String aboutText = "Arduino Electronic Load - Designed by KAUSHAL   ";
  int textLength = aboutText.length();
  static int scrollIndex = 0;
  static unsigned long lastScrollTime = 0;
  const int scrollSpeed = 300;  // Adjust speed (milliseconds)

  if (millis() - lastScrollTime >= scrollSpeed) {
    lastScrollTime = millis();

    // Extract 16-character portion of the text
    String displayText = aboutText.substring(scrollIndex, scrollIndex + 16);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("About:");
    lcd.setCursor(0, 1);
    lcd.print(displayText);

    // Scroll the text by moving the window
    scrollIndex++;
    if (scrollIndex > textLength - 16) {  // Reset scrolling when reaching the end
      scrollIndex = 0;
    }
  }
}

void logKeys(String logMessage) {
  File logFile = SD.open("logKeys.txt", FILE_WRITE);
  if (logFile) {
    logFile.println(logMessage);
    logFile.close();
  } else {
    Serial.println("Error writing to logKeys.txt");
  }
}

void logPower(String logMessage) {
  if (currentState == MONITOR_POWER) {  // Ensure logs are only written in Monitor mode
    File logFile = SD.open("logPower.txt", FILE_WRITE);
    if (logFile) {
      logFile.println(logMessage);
      logFile.close();
    } else {
      Serial.println("Error writing to logPower.txt");
    }
  }
}