#include <EEPROM.h>
#include <PID_v1.h>
#include <PID_AutoTune_v0.h>

// Debug info over Serial
#define SERIAL_DEBUG

#define STANDBY 0
#define COOK    1
#define CONFIG  2
#define ERR     3
#define SAVE    4
#define AUTO    5

#define BUTTON_LEFT   1<<0
#define BUTTON_RIGHT  1<<1
#define BUTTON_UP     1<<2
#define BUTTON_DOWN   1<<3

#ifdef SERIAL_DEBUG
#define BUTTON_HELP 1<<4
bool printed = false;
#endif

#define BUTTON_LEFT_PIN   4
#define BUTTON_RIGHT_PIN  5
#define BUTTON_UP_PIN     6
#define BUTTON_DOWN_PIN   7 

// A0 is 14.
#define TEMP_PIN  A0
#define PID_PIN   12

#define TPC_WINDOW 5000

struct settings_t {
  double Kp, Ki, Kd;
  int running;
} settings;

unsigned int mode = STANDBY;
unsigned int oldMode = mode;
double temp = 0;
double target = 60;
double output = 0;
unsigned int buttons = 0;

PID myPID(&temp, &output, &target, 0, 0, 0, P_ON_M, DIRECT);

uint32_t doOutput (uint32_t currentTime) {
  static uint32_t startTime = currentTime;
  uint32_t now = currentTime;
  if (now - startTime > TPC_WINDOW * CORE_TICK_RATE)
    startTime = now;
  digitalWrite(PID_PIN, (now - startTime < output * CORE_TICK_RATE));
  return (currentTime + 50 * CORE_TICK_RATE); //50ms
}

void eeprom_write_block(const void *data, const unsigned int addr, size_t len) {
  unsigned int i;
  char *cdata = (char *) data;
  for (i = 0; len > i; ++i) {
    EEPROM.write(addr+i, cdata[i]);
  }
}

void eeprom_read_block(const void *data, const unsigned int addr, size_t len) {
  unsigned int i;
  char *cdata = (char *) data;
  for (i = 0; len > i; ++i) {
    cdata[i] = EEPROM.read(addr+i);
  }
}

void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(9600);
  while(!Serial);
  Serial.println("Sous Vide Cooker");
  //printStatusSerial();
#endif
  EEPROM.setMaxAddress(sizeof(settings));
  eeprom_read_block((void *) &settings, 0, sizeof(settings));

  attachCoreTimerService(doOutput);
  
  setupPins();
    
  myPID.SetTunings(settings.Kp, settings.Ki, settings.Kd);
  myPID.SetOutputLimits(0, TPC_WINDOW);
  
  disablePID();
  changeMode(STANDBY);
}


inline void setupPins() {
  pinMode(BUTTON_UP_PIN, INPUT);
  digitalWrite(BUTTON_DOWN_PIN, HIGH);
  pinMode(BUTTON_DOWN_PIN, INPUT);
  digitalWrite(BUTTON_RIGHT_PIN, HIGH);
  pinMode(BUTTON_RIGHT_PIN, INPUT);
  digitalWrite(BUTTON_RIGHT_PIN, HIGH);
  pinMode(BUTTON_LEFT_PIN, INPUT);
  digitalWrite(BUTTON_LEFT_PIN, HIGH);
  pinMode(TEMP_PIN, INPUT);
  pinMode(PID_PIN, OUTPUT);
}

void disablePID() {
  myPID.SetMode(MANUAL);
  settings.running = false;
  output = 0;
  detachCoreTimerService(doOutput);

  digitalWrite(PID_PIN, LOW);

}

void enablePID() {
  myPID.SetMode(AUTOMATIC);
  attachCoreTimerService(doOutput);
  settings.running = true;
}

void getTemp() {
  // Prolly will end up with a onewire temp probe, will be asynchronous readings.
  // if temp ready read and start a new conversion,
  // otherwise return.
  int i = analogRead(TEMP_PIN);
  temp = 60; //Actually do this.
}


void getButtons() {
   buttons = 0;
  if(!digitalRead(BUTTON_LEFT_PIN))
    buttons |= BUTTON_LEFT;
  if(!digitalRead(BUTTON_RIGHT_PIN))
    buttons |= BUTTON_RIGHT;
  if(!digitalRead(BUTTON_UP_PIN))
    buttons |= BUTTON_UP;
  if(!digitalRead(BUTTON_DOWN_PIN))
    buttons |= BUTTON_DOWN;
    
#ifdef SERIAL_DEBUG
  char c = 0;
  if(Serial.available())
    c = Serial.read();
    switch (c) {
      case 'w':
        buttons |= BUTTON_UP;
        break;
      case 'a':
        buttons |= BUTTON_LEFT;
        break;
      case 's':
        buttons |= BUTTON_DOWN;
        break;
      case 'd':
        buttons |= BUTTON_RIGHT;
        break;
      case '?':
        buttons |= BUTTON_HELP;
        break;

    }
   // Serial.print(buttons, BIN);
#endif
}

void err() {
  disablePID();
}

void standby() {
  disablePID();
#ifdef SERIAL_DEBUG
  if(buttons & BUTTON_HELP || not printed) {
    printed = true;
    Serial.println("Standby");
    printStatusSerial();
    Serial.println("d) Configuration");
    Serial.println("w) AutoTune");
    Serial.println("s) Save Config");
  }
#endif
  if(buttons & BUTTON_RIGHT)
    changeMode(CONFIG);
  if(buttons & BUTTON_DOWN)
    changeMode(SAVE);
  if(buttons & BUTTON_UP)
    changeMode(AUTO);
}

#ifdef SERIAL_DEBUG
void printStatusSerial() {
  Serial.println("------------");
  Serial.print("Temp\t");
  Serial.print(temp, DEC);
  Serial.println();
  Serial.println("Target\t");
  Serial.print(target, DEC);
  Serial.println();
  if(settings.running)
    Serial.println("PID\tON");
  else
    Serial.println("PID\tOFF");
  Serial.println("------------");
}
#endif
void config() {
#ifdef SERIAL_DEBUG
  if(buttons & BUTTON_HELP || not printed) {
    printed = true;
    Serial.println("Config");
    printStatusSerial();
    Serial.println("a) Standby");
    Serial.println("d) Start");
    Serial.println("w) Increase Target");
    Serial.println("s) Decrease Target");
  }
#endif
  if(buttons & BUTTON_LEFT)
    changeMode(STANDBY);
  if(buttons & BUTTON_RIGHT)
    changeMode(COOK);
  if(buttons & BUTTON_UP) {
    target += 0.25;
    printed = false;
  }
  if(buttons & BUTTON_DOWN) {
    target -= 0.25;
    printed = false;
  }
}

void save() {
#ifdef SERIAL_DEBUG
  Serial.println("Saving PID settings");
#endif
  settings.Kp = myPID.GetKp();
  settings.Ki = myPID.GetKi();
  settings.Kd = myPID.GetKd();
  eeprom_write_block((const void *)&settings, 0, sizeof(settings));
  changeMode(STANDBY);
}

void autoTune() {
  #ifdef SERIAL_DEBUG
  Serial.println("Autotuning...");
  Serial.println("a) Cancel");
#endif
  //LCD clear
  // Autotuning... Temp may be erratic. Do not run unless equilibrium.
  PID_ATune autoTuner(&temp, &output);
  autoTuner.SetNoiseBand(0.5);
  autoTuner.SetOutputStep(1);
  autoTuner.SetLookbackSec(10);
  autoTuner.SetControlType(1); // PID
  do {
    getTemp();
    getButtons();
    if(buttons & BUTTON_LEFT) {
      autoTuner.Cancel();
      changeMode(STANDBY);
      return;
    }
  } while(0 == autoTuner.Runtime());
  myPID.SetTunings(autoTuner.GetKp(), autoTuner.GetKp(), autoTuner.GetKd());
  changeMode(STANDBY);
}

void changeMode(unsigned int newMode) {
  oldMode = mode;
  mode = newMode;
  printed = false;
}
void cook() {
  if(not settings.running)
    enablePID();
  #ifdef SERIAL_DEBUG
  if(buttons & BUTTON_HELP || not printed) {
    printed = true;
    Serial.println("Cook");
    printStatusSerial();
    Serial.println("a) Config");
  }
  #endif
  // print display
  if(buttons & BUTTON_LEFT)
    changeMode(CONFIG);
}

void loop() {
  getTemp();
  if (settings.running)
    myPID.Compute();
  getButtons();
  switch (mode) {
    case STANDBY:
      standby();
      break;
    case SAVE:
      save();
      break;
    case AUTO:
      autoTune();
      break;
    case CONFIG:
      config();
      break;
    case COOK:
      cook();
      break;
    default:
      err();
  }
  delay(200);
}
