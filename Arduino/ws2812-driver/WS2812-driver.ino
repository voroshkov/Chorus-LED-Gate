/**
 * Pluggable LED module for Chorus RF Laptimer by Andrey Voroshkov (bshep)
 * fast port I/O code from http://masteringarduino.blogspot.com.by/2013/10/fastest-and-smallest-digitalread-and.html

The MIT License (MIT)

Copyright (c) 2017 by Andrey Voroshkov (bshep)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <EEPROM.h>
#include <FastLED.h>
#include <PinChangeInterrupt.h>

#define INTERRUPT_PIN 12
// #define INTERRUPT_BTN1 3
// #define INTERRUPT_BTN2 2
#define LED_DATA_PIN 5

#define COLOR_ORDER GRB
#define CHIPSET     WS2811

#define NUM_LEDS    216
#define NO_FIRE_LEDS 10
#define LOW_FIRE_LEDS 70
#define TOP_FIRE_LEDS NUM_LEDS

#define NUM_COUNDOWN_COLORS 4

#define MAX_MODULES_COUNT 8
#define MAX_COLORS_COUNT 8 // TODO: use this const in colors/palettes definition arrays
#define MAX_THRESHOLD_SETUP_STATUSES_COUNT 3

// effects
#define EFFECT_FLASH_DISSOLVE         0
#define EFFECT_ETERNAL_FIRE           1
#define EFFECT_FADE_TO_BLACK          2
#define EFFECT_RISE_FIRE              3
#define EFFECT_KEEP_TOP_FIRE_LEVEL    4
#define EFFECT_TAME_FIRE              5
#define EFFECT_FIRE_OFF               6
#define EFFECT_KEEP_MIN_FIRE_LEVEL    7
#define EFFECT_SHOW_COLORS            8
#define EFFECT_SHOW_THRESHOLD_SETUP   9

// single effect phases
#define START_PHASE         0
#define WORK_CYCLE_PHASE    1
#define END_PHASE           2

#define CMD_PREPARE     0x11
#define CMD_COUNTDOWN_3 0x12
#define CMD_COUNTDOWN_2 0x13
#define CMD_COUNTDOWN_1 0x14
#define CMD_START_RACE  0x15
#define CMD_END_RACE    0x16
#define CMD_SHOW_COLORS 0x17
#define CMD_THRESHOLD_SETUP_START 0x1E
#define CMD_THRESHOLD_SETUP_STOP 0x1F

#define CMD_LAP_DRONE_1 0x21
#define CMD_LAP_DRONE_8 0x28

#define CMD_SET_COLOR_INDEX_HI 0x30
#define CMD_SETUP_THRESHOLD_STATUS_HI 0xB0 // 0x30 + 0x80 (because 8 colors might be used for color commands)
#define CMD_SET_MODULES_COUNT_HI 0xE0

#define EEPROM_MODULES_COUNT_OFFSET   10
#define EEPROM_COLORS_OFFSET          11

#define DEFAULT_MODULES_COUNT 4

CRGB leds[NUM_LEDS];

CHSV countdownCHSVColors[NUM_COUNDOWN_COLORS] = {
    CHSV(96, 255, 255), // green
    CHSV(20, 255, 255),
    CHSV(10, 255, 255),
    CHSV(0, 255, 255)   // red
};

volatile uint8_t isNewEventOccurred = 0;
volatile uint8_t eventId = CMD_END_RACE;

uint8_t currentEffect = EFFECT_ETERNAL_FIRE;
uint32_t effectDuration = 0; // zero means forever. otherwise - desired effect duration in ms
uint8_t isNewEffect = 1; // flag that indicates new captured interrupt

// these vars are specific to different effects
uint8_t effectBrightness = 255; // used in flash'n'dissolve effect
CHSV effectHSVColor; // used in flash'n'dissolve effect
uint8_t effectPaletteIndex = MAX_COLORS_COUNT; // used in fire effect
uint8_t fireNumLeds = LOW_FIRE_LEDS; // used in fire effect

// Array of fire temperature readings at each simulation cell
byte heat[NUM_LEDS];

DEFINE_GRADIENT_PALETTE( heatmap_gp ) {
      0,     0,  0,  0,   //black
    100,   255,  0,  0,   //red
    190,   255,255,  0,   //bright yellow
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( redFire_gp ) {
      0,     0,  0,  0,   //black
    100,   255,  0,  0,   //red
    190,   255,90,  0,
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( blueFire_gp ) {
      0,     0,  0,  0,   //black
     30,   0, 17, 10,
    120,   0,  0,  250,   //blue
    199,   200, 150, 200,
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( greenFire_gp ) {
      0,     0,  0,  0,   //black
     30,   10, 20, 0,
    100,   0,  240,  0,   //green
    190,   200,255,  0,   //yellow
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( yellowFire_gp ) {
      0,     0,  0,  0,   //black
     30,   50, 20, 0,     //darker orange
    130,   255, 255, 0,   //yellow
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( orangeFire_gp ) {
      0,     0,  0,  0,   //black
     30,   50, 20, 0,     //darker orange
    130,   255, 140, 0,   //orange
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( purpleFire_gp ) {
      0,     0,  0,  0,   //black
     30,   50, 20, 0,     //darker orange
    130,   160, 32, 240,   //purple
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( aquaFire_gp ) {
      0,     0,  0,  0,   //black
     30,   50, 20, 0,     //darker orange
    130,   107, 255, 212,   //aquamarine
    255,   255,255,255    //white
};

DEFINE_GRADIENT_PALETTE( pinkFire_gp ) {
      0,     0,  0,  0,   //black
     30,   139, 10, 80,   //darker pink
     30,   255, 20, 150,  //pink
    255,   255,255,255    //white
};

// the following arrays should have matching colors up to MAX_COLORS_COUNT: red, yellow, green, blue
CRGBPalette16 firePalettesList [MAX_COLORS_COUNT + 1] = { redFire_gp, yellowFire_gp, greenFire_gp, blueFire_gp, orangeFire_gp, aquaFire_gp, purpleFire_gp, pinkFire_gp, HeatColors_p };
CHSV droneColors[MAX_COLORS_COUNT] = {
    CHSV(0, 255, 255), // red
    CHSV(64, 255, 255),  // yellow
    CHSV(96, 255, 255), // green
    CHSV(160, 255, 255), // blue
    CHSV(32, 255, 255), // orange
    CHSV(128, 255, 255), // aqua
    CHSV(192, 255, 255), // purple
    CHSV(224, 255, 255), // pink
};

uint8_t moduleToColorMap[MAX_MODULES_COUNT]; // read from EEPROM on setup, set via commands

uint8_t modulesInUse = DEFAULT_MODULES_COUNT; // number of modules in use; needed for calibration and colors demo

uint8_t thresholdSetupPhases[MAX_MODULES_COUNT];

// ----------------------------------------------------------------------------
void setup() {
    DDRB = 0; //all 8-13 are inputs
    PORTB = 0xFF; //all 8-13 pull-up, values will be inverted

    pinMode(INTERRUPT_PIN, INPUT_PULLUP);
    // pinMode(INTERRUPT_BTN1, INPUT_PULLUP);
    // pinMode(INTERRUPT_BTN2, INPUT_PULLUP);

    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);

    delay(100); //just don't use LEDs immediately anyway

    FastLED.show(); // Initialize all pixels to 'off'

    random16_add_entropy(28);   // for module 1
    // random16_add_entropy(173);   // for module 2

    readModulesCountFromEEPROM();

    readModuleColorsFromEEPROM();

    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_PIN), intHandler, CHANGE);
    // attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_BTN1), btnHandler1, FALLING);
    // attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_BTN2), btnHandler2, FALLING);

    // Serial.begin(115200);
}

// ----------------------------------------------------------------------------
void loop() {
    if (isNewEventOccurred) {
        isNewEventOccurred = 0;
        // Serial.print("event ");
        // Serial.println(eventId);
        if (eventId >= CMD_LAP_DRONE_1 && eventId <= CMD_LAP_DRONE_8) { // drone passed
            setNewEffect(EFFECT_RISE_FIRE, 140); // it's a sequence of effects actually
            uint8_t moduleId = eventId - CMD_LAP_DRONE_1;
            effectPaletteIndex = moduleToColorMap[moduleId];

        } else if (eventId == CMD_COUNTDOWN_3) { // start sequence
            setNewEffect(EFFECT_FLASH_DISSOLVE, 1500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[3];

        } else if (eventId == CMD_COUNTDOWN_2) { // start sequence
            setNewEffect(EFFECT_FLASH_DISSOLVE, 1500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[2];

        } else if (eventId == CMD_COUNTDOWN_1) { // start sequence
            setNewEffect(EFFECT_FLASH_DISSOLVE, 1500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[1];

        } else if (eventId == CMD_START_RACE) { // start race
            setNewEffect(EFFECT_FLASH_DISSOLVE, 2500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[0];

        } else if (eventId == CMD_END_RACE) { // race finished, switch to default effect
            fireNumLeds = LOW_FIRE_LEDS;
            setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            effectPaletteIndex = MAX_COLORS_COUNT; // real fire heatmap is the last in palette

        } else if (eventId == CMD_PREPARE) { // prepare to race
            if (currentEffect == EFFECT_ETERNAL_FIRE) {
                setNewEffect(EFFECT_FIRE_OFF, 1000);
            }
        } else if (eventId == CMD_SHOW_COLORS) { // prepare to race
            setNewEffect(EFFECT_SHOW_COLORS, modulesInUse * 700);

        } else if (eventId >= CMD_SET_MODULES_COUNT_HI && eventId <= CMD_SET_MODULES_COUNT_HI + MAX_MODULES_COUNT) { // set used number of modules
            setModulesCount(eventId - CMD_SET_MODULES_COUNT_HI);
            setNewEffect(EFFECT_SHOW_COLORS, modulesInUse * 700);

        } else if (eventId == CMD_THRESHOLD_SETUP_START) { // start setting up threshold
            clearThresholdSetupPhases();
            setNewEffect(EFFECT_SHOW_THRESHOLD_SETUP, 0);

        } else if (eventId == CMD_THRESHOLD_SETUP_STOP) { // stop setting up threshold
            clearThresholdSetupPhases();
            fireNumLeds = LOW_FIRE_LEDS;
            setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            effectPaletteIndex = MAX_COLORS_COUNT;

        } else if ((eventId >= CMD_SET_COLOR_INDEX_HI) && (eventId <= CMD_SET_COLOR_INDEX_HI + (MAX_COLORS_COUNT << 4))) {
            setColorForModule((eventId - CMD_SET_COLOR_INDEX_HI) >> 4 , (eventId & 0xF) - 1); // decrease module idx by 1 to use zero-based indexes

        } else if ((eventId >= CMD_SETUP_THRESHOLD_STATUS_HI) && (eventId <= CMD_SETUP_THRESHOLD_STATUS_HI + (MAX_THRESHOLD_SETUP_STATUSES_COUNT << 4))) {
            uint8_t moduleId = (eventId & 0xF) - 1; // decrease module idx by 1 to use zero-based indexes
            uint8_t statusId = (eventId - CMD_SETUP_THRESHOLD_STATUS_HI) >> 4;
            thresholdSetupPhases[moduleId] = statusId;
            if (statusId == 1) {
                clearThresholdSetupPhases();
                setNewEffect(EFFECT_SHOW_THRESHOLD_SETUP, 0);
            }
        }
    }

    runCurrentEffect();

    FastLED.show();
}

// ----------------------------------------------------------------------------
void intHandler(void) {
    static volatile uint8_t commandSequenceId = 0;
    static volatile uint8_t commandHi;
    static volatile uint8_t commandLo;


    // portb data is inverted
    volatile uint8_t data = (~PINB) & 0xF;
    switch(commandSequenceId) {
        case 0:
            if (data == 0) {
                commandSequenceId = 1;
            }
            break;
        case 1:
            commandHi = data;
            commandSequenceId = 2;

            break;
        case 2:
            commandLo = data;
            commandSequenceId = 0;
            isNewEventOccurred = 1;
            eventId = (commandHi << 4) | commandLo;
            break;
    }
}

// // TODO: remove this handler ((((((DEBUG ONLY))))))
// void btnHandler1(void) {
//     effectPaletteIndex = ++effectPaletteIndex % 4;
// }

// // TODO: remove this handler ((((((((DEBUG ONLY))))))))
// void btnHandler2(void) {
//     setNewEffect(EFFECT_FIRE_OFF, 120);
// }

void clearThresholdSetupPhases() {
    for(int i = 0; i < MAX_MODULES_COUNT; i++) {
        if (thresholdSetupPhases[i] != 1) {
            thresholdSetupPhases[i] = 255; // some non-existing value
        }
    }
}

void readModuleColorsFromEEPROM() {
    for(uint8_t i = 0; i < MAX_MODULES_COUNT; i++) {
        uint8_t value = EEPROM.read(i + EEPROM_COLORS_OFFSET);
        if (value > MAX_COLORS_COUNT) {
            value = 0;
        }
        moduleToColorMap[i] = value;
    }
}


void setColorForModule(uint8_t colorIdx, uint8_t moduleIdx) {
    if (colorIdx < MAX_COLORS_COUNT && moduleIdx < MAX_MODULES_COUNT) {
        EEPROM.update(moduleIdx + EEPROM_COLORS_OFFSET, colorIdx);
        moduleToColorMap[moduleIdx] = colorIdx;
    }
}

void readModulesCountFromEEPROM() {
    uint8_t count = EEPROM.read(EEPROM_MODULES_COUNT_OFFSET);
    setModulesCount(count); // this will update EEPROM to default value if the reading is invalid
}

void setModulesCount(uint8_t count) {
    modulesInUse = count;
    if (modulesInUse == 0) {
        modulesInUse = DEFAULT_MODULES_COUNT;
    }
    EEPROM.update(EEPROM_MODULES_COUNT_OFFSET, count);
}

void runCurrentEffect() {
    switch(currentEffect) {
        case EFFECT_FLASH_DISSOLVE:
            effectRunner(effectInstantFlashAndDissolve, 30);
            break;
        case EFFECT_ETERNAL_FIRE:
            effectRunner(fireEffect, 60);
            break;
        case EFFECT_FADE_TO_BLACK:
            effectRunner(fadeToBlack, 30);
            break;
        case EFFECT_RISE_FIRE:
            effectRunner(riseFire, 60);
            break;
        case EFFECT_KEEP_TOP_FIRE_LEVEL:
            effectRunner(keepTopFireLevel, 60);
            break;
        case EFFECT_TAME_FIRE:
            effectRunner(tameFire, 60);
            break;
        case EFFECT_FIRE_OFF:
            effectRunner(fireOff, 60);
            break;
        case EFFECT_KEEP_MIN_FIRE_LEVEL:
            effectRunner(keepMinimumFireLevel, 60);
            break;
        case EFFECT_SHOW_COLORS:
            effectRunner(showColorsEffect, 30);
            break;
        case EFFECT_SHOW_THRESHOLD_SETUP:
            effectRunner(showThresholdSetupEffect, 60);
            break;
    }
}

void effectRunner(void (*effect)(uint8_t, uint32_t), uint16_t fps) {
    uint16_t frameDelay = 1000/fps;
    static uint32_t effectStartTime;
    static uint32_t frameStartTime;

    if (isNewEffect) {
        isNewEffect = 0;
        effectStartTime = millis();
        frameStartTime = effectStartTime;
        (*effect)(START_PHASE, 0); // effect phase 0 (start)
    } else {
        if (!checkIsNewFrame(&frameStartTime, frameDelay)) return;

        uint32_t time = millis();
        uint32_t diff = time - effectStartTime;
        if (effectDuration > 0 && diff > effectDuration) {
            (*effect)(END_PHASE, effectDuration); // effect phase 2 (end)
        } else {
            (*effect)(WORK_CYCLE_PHASE, diff); // effect phase 1 (work cycle)
        }
    }
}

uint8_t checkIsNewFrame(uint32_t *frameStartTime, uint8_t frameDelay) {
    uint32_t time = millis();

    uint32_t frameDiff = time - *frameStartTime;
    if (frameDiff < frameDelay) {
        return false;
    }

    *frameStartTime = time;
    return true;
}

void setNewEffect(uint8_t effectId, uint32_t effectDur) {
    currentEffect = effectId;
    effectDuration = effectDur;
    isNewEffect = 1;
}

void effectInstantFlashAndDissolve(uint8_t phase, uint32_t elapsedTime) {
    uint32_t curBrightness;
    switch(phase) {
        case START_PHASE:
            for(uint8_t i = 0; i < NUM_LEDS; i++) {
                CHSV color = effectHSVColor;
                color.v = effectBrightness;
                leds[i] = color;
            }
            break;
        case WORK_CYCLE_PHASE:
            curBrightness = map(elapsedTime, 0, effectDuration, effectBrightness, 0);
            for(uint8_t i = 0; i < NUM_LEDS; i++) {
                CHSV color = effectHSVColor;
                color.v = qsub8(curBrightness, random8(0, 15));
                leds[i] = color;
            }
            break;
        case END_PHASE:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            break;
    }
}

void showColorsEffect(uint8_t phase, uint32_t elapsedTime) {
    #define COLOR_BREAK_LENGTH 4 // number of leds to remain black between colored areas

    uint8_t sectionLength = NUM_LEDS / modulesInUse;
    uint8_t colorLedsCount = sectionLength - COLOR_BREAK_LENGTH;
    elapsedTime = constrain(elapsedTime, 0, effectDuration);
    uint32_t moduleTime = (effectDuration / modulesInUse) / 2; // show in sequence for half-time, then just let shine for another half
    uint32_t fadeInTime = moduleTime / 2;
    uint8_t currentModuleIdx = constrain(elapsedTime / moduleTime, 0, modulesInUse - 1);
    uint32_t currentModuleTime = elapsedTime - moduleTime * currentModuleIdx;
    uint8_t startLed = sectionLength * currentModuleIdx + COLOR_BREAK_LENGTH;

    uint8_t brightness = 0;
    CHSV moduleColor = droneColors[moduleToColorMap[currentModuleIdx]];

    switch(phase) {
        case START_PHASE:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            break;
        case WORK_CYCLE_PHASE:
            if (currentModuleTime < fadeInTime) {
                brightness = map(currentModuleTime, 0, fadeInTime, 0, 255);
            } else {
                brightness = 255;
            }

            moduleColor.v = brightness;

            // ------ set COLOR_BREAK_LENGTH leds to black
            // for(uint8_t j = 0; j < COLOR_BREAK_LENGTH; j++) {
            //     uint8_t startLed = sectionLength * currentModuleIdx;
            //     CRGB black = CRGB(0,0,0);
            //     leds[startLed + j] = black;
            // }

            for(uint8_t j = startLed; j < startLed + colorLedsCount; j++) {
                leds[j] = moduleColor;
            }
            break;
        case END_PHASE:
            setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            break;
    }
}

void showThresholdSetupEffect(uint8_t phase, uint32_t elapsedTime) {
    #define COLOR_BREAK_LENGTH 4 // number of leds to remain black between colored areas
    #define SLOW_PULSE_DURATION 1500
    #define SLOW_PULSE_CHANGE_TIME SLOW_PULSE_DURATION * 0.4
    #define FAST_PULSE_DURATION 1000
    #define FAST_PULSE_CHANGE_TIME FAST_PULSE_DURATION * 0.5
    #define MIN_SLOW_BRIGHTNESS 5
    #define MAX_SLOW_BRIGHTNESS 150
    #define MIN_FAST_BRIGHTNESS 50
    #define MAX_FAST_BRIGHTNESS 250
    #define SOLID_BRIGHTNESS 255

    uint8_t sectionLength = NUM_LEDS / modulesInUse;
    uint8_t colorLedsCount = sectionLength - COLOR_BREAK_LENGTH;

    // uint32_t moduleTime = (effectDuration / modulesInUse) / 2; // show in sequence for half-time, then just let shine for another half
    // uint32_t fadeInTime = moduleTime / 2;
    // uint8_t currentModuleIdx = constrain(elapsedTime / moduleTime, 0, modulesInUse - 1);
    // uint32_t currentModuleTime = elapsedTime - moduleTime * currentModuleIdx;

    uint32_t slowPulseTime = elapsedTime % SLOW_PULSE_DURATION;
    uint8_t slowBrightness = MAX_SLOW_BRIGHTNESS;
    uint8_t slowDimmedLeds = 0;
    if (slowPulseTime < SLOW_PULSE_CHANGE_TIME) {
        slowBrightness = map(slowPulseTime, 0, SLOW_PULSE_CHANGE_TIME, MIN_SLOW_BRIGHTNESS, MAX_SLOW_BRIGHTNESS);
        slowDimmedLeds = map(slowPulseTime, 0, SLOW_PULSE_CHANGE_TIME, 0, 20);
    } else if (slowPulseTime > (SLOW_PULSE_DURATION - SLOW_PULSE_CHANGE_TIME)) {
        slowBrightness = map(slowPulseTime, SLOW_PULSE_DURATION - SLOW_PULSE_CHANGE_TIME, SLOW_PULSE_DURATION, MAX_SLOW_BRIGHTNESS, MIN_SLOW_BRIGHTNESS);
        slowBrightness = map(slowPulseTime, SLOW_PULSE_DURATION - SLOW_PULSE_CHANGE_TIME, SLOW_PULSE_DURATION, 20, 0);
    }

    uint32_t fastPulseTime = elapsedTime % FAST_PULSE_DURATION;
    uint8_t fastBrightness = MAX_FAST_BRIGHTNESS;
    uint8_t fastDimmedLeds = 0;
    if (fastPulseTime < FAST_PULSE_CHANGE_TIME) {
        fastBrightness = map(fastPulseTime, 0, FAST_PULSE_CHANGE_TIME, MIN_FAST_BRIGHTNESS, MAX_FAST_BRIGHTNESS);
        fastDimmedLeds = map(fastPulseTime, 0, FAST_PULSE_CHANGE_TIME, 0, colorLedsCount * 2 / 5);
    } else if (fastPulseTime > (FAST_PULSE_DURATION - FAST_PULSE_CHANGE_TIME)) {
        fastBrightness = map(fastPulseTime, FAST_PULSE_DURATION - FAST_PULSE_CHANGE_TIME, FAST_PULSE_DURATION, MAX_FAST_BRIGHTNESS, MIN_FAST_BRIGHTNESS);
        fastDimmedLeds = map(fastPulseTime, FAST_PULSE_DURATION - FAST_PULSE_CHANGE_TIME, FAST_PULSE_DURATION, colorLedsCount * 2 / 5, 0);
    }

    switch(phase) {
        case START_PHASE:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            break;
        case WORK_CYCLE_PHASE:
            for(uint8_t i = 0; i < modulesInUse; i++) {
                // show state of a module
                uint8_t startLed = sectionLength * i;
                uint8_t setupPhase = thresholdSetupPhases[i];
                CHSV moduleColor = droneColors[moduleToColorMap[i]];
                switch(setupPhase) {
                    case 0:
                        moduleColor.v = SOLID_BRIGHTNESS;
                        for(uint8_t j = startLed; j < startLed + colorLedsCount; j++) {
                            leds[j] = moduleColor;
                        }
                        break;
                    case 1:
                        moduleColor.v = random8(MIN_SLOW_BRIGHTNESS, MAX_SLOW_BRIGHTNESS);
                        for(uint8_t j = startLed; j < startLed + colorLedsCount; j++) {
                            leds[j] = moduleColor;
                        }
                        for(uint8_t k = 0; k < 5; k++) {
                            uint8_t startPos = startLed + random8(0, sectionLength - 6);
                            for(uint8_t m = 0; m < 6; m++) {
                                leds[startPos + m] = CRGB::Black;
                            }
                        }
                        break;
                    case 2:
                        for(uint8_t j = startLed; j < startLed + colorLedsCount; j++) {
                            if (j-startLed > fastDimmedLeds && j < startLed + colorLedsCount - fastDimmedLeds) {
                                moduleColor.v = MAX_FAST_BRIGHTNESS;
                            } else {
                                moduleColor.v = 0;
                            }
                            leds[j] = moduleColor;
                        }
                        break;
                    default:
                        moduleColor.v = 0;
                        for(uint8_t j = startLed; j < startLed + colorLedsCount; j++) {
                            leds[j] = moduleColor;
                        }
                }
            }
            break;
        case END_PHASE:
            // setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            break;
    }
}

void fadeToBlack (uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            break;
        case WORK_CYCLE_PHASE:
            fade_raw(&leds[0], NUM_LEDS, 20);
            break;
        case END_PHASE:
            break;
    }
}

void fireEffect(uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            // set it all black to start fire from the bottom up to fireNumLeds
            fill_solid(leds, NUM_LEDS, CHSV(0, 0, 0));
            for( int i = 0; i < fireNumLeds; i++) {
                heat[i] = 0;
            }
            fireNumLeds = LOW_FIRE_LEDS;
            break;
        case WORK_CYCLE_PHASE:
            continueFire();
            break;
        case END_PHASE:
            break;
    }
}

void riseFire(uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            continueFire();
            break;
        case WORK_CYCLE_PHASE:
            fireNumLeds = map(elapsedTime, 0, effectDuration, LOW_FIRE_LEDS, TOP_FIRE_LEDS);
            // fill heat with a gradient to current top
            for( int i = 0; i < fireNumLeds; i++) {
                heat[i] =  map(i, 0, fireNumLeds, 255, 0);
            }
            continueFire(); // continue fire with new number of leds
            break;
        case END_PHASE:
            setNewEffect(EFFECT_KEEP_TOP_FIRE_LEVEL, 800);
            break;
    }
}

void keepTopFireLevel (uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            fireNumLeds = TOP_FIRE_LEDS;
            continueFire(); // continue fire with new number of leds
            break;
        case WORK_CYCLE_PHASE:
            continueFire();
            break;
        case END_PHASE:
            setNewEffect(EFFECT_TAME_FIRE, 2000);
            break;
    }
}

void tameFire(uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            continueFire(); // continue fire with new number of leds
            break;
        case WORK_CYCLE_PHASE:
            fireNumLeds = map(elapsedTime, 0, effectDuration, TOP_FIRE_LEDS, LOW_FIRE_LEDS);
            continueFire();
            break;
        case END_PHASE:
            fireNumLeds = LOW_FIRE_LEDS;
            setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            isNewEffect = 0; // hack to prevent fire starting from down
            break;
    }
}

void fireOff(uint8_t phase, uint32_t elapsedTime) {
    static uint8_t startFireLeds;
    switch(phase) {
        case START_PHASE:
            startFireLeds = fireNumLeds;
            continueFire();
            break;
        case WORK_CYCLE_PHASE:
            fireNumLeds = map(elapsedTime, 0, effectDuration, startFireLeds, NO_FIRE_LEDS); // 3 is the lowest possible leds count for continueFire() to work properly
            continueFire();
            break;
        case END_PHASE:
            setNewEffect(EFFECT_KEEP_MIN_FIRE_LEVEL, 2000);
            break;
    }
}

void keepMinimumFireLevel (uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            fireNumLeds = NO_FIRE_LEDS;
            continueFire(); // continue fire with new number of leds
            break;
        case WORK_CYCLE_PHASE:
            continueFire();
            break;
        case END_PHASE:
            fill_solid(leds, NUM_LEDS, CHSV(0, 0, 0));
            setNewEffect(EFFECT_FADE_TO_BLACK, 0);
            break;
    }
}

void continueFire() {
    // COOLING: How much does the air cool as it rises?
    // Less cooling = taller flames.  More cooling = shorter flames.
    // Default 55, suggested range 20-100
    #define COOLING 50

    // SPARKING: What chance (out of 255) is there that a new spark will be lit?
    // Higher chance = more roaring fire.  Lower chance = more flickery fire.
    // Default 120, suggested range 50-200.
    #define SPARKING 120

    //---------------------
    // Step 1.  Cool down every cell (up to the top) a little
    for( int i = 0; i < fireNumLeds; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / fireNumLeds) + 2));
    }

    // process items above the fire top (to kill flames that remained after big fire)
    for( int i = fireNumLeds; i < NUM_LEDS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }

    //---------------------
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= fireNumLeds - 3; k > 0; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }

    // process items above the fire top (to kill flames that remained after big fire)
    for( int k= NUM_LEDS - 3; k > fireNumLeds; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }

    //---------------------
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
        // Scale the heat value from 0-255 down to 0-240
        // for best results with color palettes.
        byte colorindex = scale8( heat[j], 240);
        CRGB color = ColorFromPalette(firePalettesList[effectPaletteIndex], colorindex);

        leds[j] = color;
    }
}
