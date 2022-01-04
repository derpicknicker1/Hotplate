#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <max6675.h>
#include <ESPRotary.h>

#define CONF_VERSION "0.0"
#define CONFIG_ADDR 0

#define COOLDOWN_TIME 60000

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64 

#define TEMP_DO_PIN D6
#define TEMP_CS_PIN D7
#define TEMP_CL_PIN D5

#define BTN_PIN D0
#define SSR_PIN D8 

#define TEMP_PREHEAT_MIN 100
#define TEMP_PREHEAT_MAX 200
#define TEMP_REFLOW_MAX  270

#define OFFSET 0

#define DEG " "+(char)247+"C"

#define ROTARY_PIN1       D4
#define ROTARY_PIN2       D3
#define CLICKS_PER_STEP   4

#define MENU_ITEMS 4

enum {CONFIG, OFF, PREHEAT, REFLOW, COOLING};
const String state_str[] = {"CONFIG", "OFF", "PREHEAT", "REFLOW", "COOLING"};
const String menuItems[MENU_ITEMS] = {"Preheat", "Reflow" , "Save", "EXIT"};

struct ConfS {
	int preheat = 0;
	int reflow = 0; 
	char vers[4];
} conf = {TEMP_PREHEAT_MIN, TEMP_PREHEAT_MAX, CONF_VERSION};

int temp = 0;
int tempSet = 0;
int tempReflowOld = 0;
int state = OFF;
int time_count = 0;
unsigned long t = millis();
unsigned long t_solder = millis();
int menuState = 0;
int menuStateOld = -1;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
MAX6675 thermocouple(TEMP_CL_PIN, TEMP_CS_PIN, TEMP_DO_PIN);
ESPRotary rotary;

void splashScreen(String txt, String head = "", int wait = 0);
void printStr(String str, int x = 0, int y = 0, int txtSize = 1);
void loadConfig();
void showConfig();
void menuMain(int btnPress);
void menuPreheat(int btnPress);
void menuReflow(int btnPress);
void menuSave(int btnPress);
void startMenu(int steps, int lBound, int uBound, int pos);
void setRotaryRange(int lBound, int uBound, int val);
void PrintScreen();
int X(int fontSize, int len);
int Y(int fontSize, float f);


void loadConfig() {
	if (EEPROM.read(CONFIG_ADDR + sizeof(ConfS) - 2) == conf.vers[2] &&
			EEPROM.read(CONFIG_ADDR + sizeof(ConfS) - 3) == conf.vers[1] &&
			EEPROM.read(CONFIG_ADDR + sizeof(ConfS) - 4) == conf.vers[0])
		EEPROM.get(CONFIG_ADDR, conf);
	else {
		EEPROM.put(CONFIG_ADDR, conf);
		EEPROM.commit();
	}
		
}


void setup() {
	Serial.begin(74880);
	EEPROM.begin(512);
	rotary.begin(ROTARY_PIN1, ROTARY_PIN2, CLICKS_PER_STEP);
	pinMode(BTN_PIN, INPUT_PULLDOWN_16);
	pinMode(SSR_PIN, OUTPUT);
	digitalWrite(SSR_PIN, LOW);
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.clearDisplay();
	display.display();
	loadConfig();
	tempReflowOld = conf.reflow;
	setRotaryRange(conf.preheat, TEMP_REFLOW_MAX, conf.reflow);
}


void loop() {

	// regulate temperature
	if (tempSet <= temp - OFFSET)
		digitalWrite(SSR_PIN, LOW);
	else if (tempSet > temp + OFFSET)
		digitalWrite(SSR_PIN, HIGH);

	// housekeeping
	rotary.loop();
	if (millis() > t + 250 || millis() < t) {
		temp = thermocouple.readCelsius();
		PrintScreen();
		t = millis();
	}

	// handle encoder change
	if ( state == REFLOW && (rotary.getPosition() != tempReflowOld)) {
		unsigned long wait = millis();
		while (millis() < wait + 2000) {
			conf.reflow = rotary.getPosition();
			if (conf.reflow != tempReflowOld)
				wait = millis();
			splashScreen(String(conf.reflow), "REFLOW", 100);
			tempReflowOld = conf.reflow;
		}
		tempSet = conf.reflow;
	}

	// handle button
	if (digitalRead(BTN_PIN)) {
		delay(100);    
		int cnt = 0;
		while (digitalRead(BTN_PIN)) {
			if (++cnt == 150) {
				splashScreen(state == OFF ? "CONFIG" : "OFF");
				state = (state == OFF ? CONFIG : OFF) - 1;
			}
			delay(10);
		}
		switch (++state) {
			default: state = OFF;
			case OFF: 
			case COOLING: tempSet = 0; t_solder = millis(); break;
			case PREHEAT: tempSet = conf.preheat; break;
			case REFLOW: tempSet = conf.reflow; rotary.resetPosition(conf.reflow); break;
			case CONFIG: break;
		}
	}

	// handle states
	if (state == REFLOW && temp >= tempSet) {
		state = COOLING;
		t_solder = millis();
		tempSet = 0;
	}
	else if (state == COOLING) {
		time_count = int((t_solder + COOLDOWN_TIME - millis()) / 1000);
		if (time_count <= 0) {
			state = OFF;
		}
	}
	else if (state == CONFIG) {
		showConfig();
		state = OFF;
	}
	else if (state != PREHEAT && state != REFLOW) {
		tempSet = 0;
		time_count = 0;
	}
}

void showConfig() {
	boolean btnPress = false;
	menuState = 0;
	menuStateOld = 1;

	while (state == CONFIG) {
		if(digitalRead(BTN_PIN)) {
			btnPress = true;
			delay(100);
		}
		rotary.loop();
		if (millis() > t + 200 || millis() < t) {
			switch (menuState) {
				default:
				case 0: menuMain(btnPress); break;
				case 1: menuPreheat(btnPress); break;
				case 2: menuReflow(btnPress); break;
				case 3: menuSave(btnPress); break;
				case 4: state = OFF; break;
			}
			t = millis();
		}
		btnPress = false;
	}
	tempReflowOld = conf.reflow;
	startMenu(1, conf.preheat, TEMP_REFLOW_MAX, conf.reflow); 
}

void menuMain(int btnPress) {
	startMenu(2, 0, MENU_ITEMS - 1, menuStateOld - 1); 
	display.fillScreen(WHITE);
	display.setTextColor(BLACK);
	for (int i=0; i < MENU_ITEMS; i++) {
		if (i == rotary.getPosition())
			printStr(">" + menuItems[i], 0, i * 16, 2);
		else
			printStr(" " + menuItems[i], 0, i * 16, 2);
	}
	display.display();
	
	menuState = btnPress ? rotary.getPosition() + 1: menuState;
}


void menuPreheat(int btnPress) {
	startMenu(1, TEMP_PREHEAT_MIN, TEMP_PREHEAT_MAX, conf.preheat); 
	conf.preheat = rotary.getPosition();
	splashScreen(String(conf.preheat), "PREHEAT");

	menuState = btnPress ? 0 : menuState;
}


void menuReflow(int btnPress) {
	startMenu(1, conf.preheat, TEMP_REFLOW_MAX, conf.reflow);
	conf.reflow = rotary.getPosition();
	splashScreen(String(conf.reflow), "REFLOW");

	menuState = btnPress ? 0 : menuState;
}


void menuSave(int btnPress) {
	startMenu(2, 0, 5, 0);
	boolean save = rotary.getPosition() > 2;
	splashScreen(save ? "> YES\n  NO " : "  YES\n> NO ", "Save to EEPROM?");
	if (btnPress && save) {
		EEPROM.put(CONFIG_ADDR, conf);
		EEPROM.commit();
	}
	menuState = btnPress ? 0 : menuState;
}


void startMenu(int steps, int lBound, int uBound, int pos) {
	if(menuState != menuStateOld) {
		menuStateOld = menuState;
		rotary.setStepsPerClick(steps * 4);
		setRotaryRange(lBound, uBound, pos);
	}
}


void setRotaryRange(int lBound, int uBound, int val) {
	rotary.setUpperBound(uBound);
	rotary.setLowerBound(lBound);
	rotary.setUpperBound(uBound); 
	rotary.resetPosition(val);
}

/*****************************************
 * DISPLAY METHODS
 ****************************************/
void printStr(String str, int x, int y, int txtSize) {
	display.setTextSize(txtSize);
	display.setCursor(x, y);
	display.print(str);
}


void PrintScreen() {
	display.clearDisplay();
	display.setTextColor(WHITE);
	printStr(state_str[state]);
	printStr(String(tempSet) +  DEG, 80);
	printStr(String(temp)  +  DEG, 30, 22, 2);
	if (time_count != 0) {
		printStr(String(time_count) + " sec", 0, 50);
	}
	if (state == PREHEAT || state == REFLOW) {
		printStr(String(int((float(temp) / float(tempSet)) * 100.00)) + " %", 80, 50);
	}
	display.display();
}


void splashScreen(String txt, String head, int wait) {
	rotary.loop();
	if (millis() > t + wait || millis() < t) {
		display.fillScreen(WHITE);
		display.setTextColor(BLACK);
		printStr(head, X(1, head.length()), Y(1, 0.1));
		printStr(txt, X(2, txt.length()), Y(2, 0.5), 2);
		display.display();
		t = millis();
	}
}


int X(int fontSize, int len) {
	return (0.5 * (display.width() - fontSize * (6 * len - 1)));
}


int Y(int fontSize, float f) {
	return (f * display.height() - (fontSize * 4));
}
