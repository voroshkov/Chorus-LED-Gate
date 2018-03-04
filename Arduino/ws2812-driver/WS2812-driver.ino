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

#include <FastLED.h>
#include <PinChangeInterrupt.h>

#define INTERRUPT_PIN 12
#define INTERRUPT_BTN1 3
#define INTERRUPT_BTN2 2
#define LED_DATA_PIN 6
#define LED_DATA_PIN2 5

#define COLOR_ORDER GRB
#define CHIPSET     WS2811

#define NUM_LEDS    144
#define NO_FIRE_LEDS 10
#define LOW_FIRE_LEDS 70
#define TOP_FIRE_LEDS NUM_LEDS

// #define NUM_COLORS  6
#define NUM_COUNDOWN_COLORS 4

// effects
#define EFFECT_FLASH_DISSOLVE       0
#define EFFECT_ETERNAL_FIRE         1
#define EFFECT_FADE_TO_BLACK        2
#define EFFECT_RISE_FIRE            3
#define EFFECT_KEEP_TOP_FIRE_LEVEL  4
#define EFFECT_TAME_FIRE            5
#define EFFECT_FIRE_OFF             6
#define EFFECT_KEEP_MIN_FIRE_LEVEL  7


// single effect phases
#define START_PHASE         0
#define WORK_CYCLE_PHASE    1
#define END_PHASE           2

// event ids
#define EVENT_DRONE_1   0
#define EVENT_DRONE_2   1
#define EVENT_DRONE_3   2
#define EVENT_DRONE_4   3
#define EVENT_START_RACE    10
#define EVENT_TONE_1        11
#define EVENT_TONE_2        12
#define EVENT_TONE_3        13
#define EVENT_PREPARE_RACE  14
#define EVENT_STOP_RACE     15


CRGB leds[NUM_LEDS];
CRGB leds2[NUM_LEDS];

CHSV countdownCHSVColors[NUM_COUNDOWN_COLORS] = {
    CHSV(96, 255, 255), // green
    CHSV(20, 255, 255),
    CHSV(10, 255, 255),
    CHSV(0, 255, 255)   // red
};

// CHSV CHSVcolors[NUM_COLORS] = {
//     CHSV(0, 255, 255), // red
//     CHSV(96, 255, 255), // green
//     CHSV(160, 255, 255), // blue
//     CHSV(64, 255, 255),  // yellow
//     CHSV(224, 255, 255),  // pink
//     CHSV(0, 0, 255)  // white
// };

volatile uint8_t isNewEventOccurred = 0;
volatile uint8_t eventId = EVENT_STOP_RACE;

uint8_t currentEffect = EFFECT_ETERNAL_FIRE;
uint32_t effectDuration = 0; // zero means forever. otherwise - desired effect duration in ms
uint8_t isNewEffect = 1; // flag that indicates new captured interrupt

// these vars are specific to different effects
uint8_t effectBrightness = 255; // used in flash'n'dissolve effect
CHSV effectHSVColor; // used in flash'n'dissolve effect
uint8_t effectPaletteIndex = 4; // used in fire effect
uint8_t fireNumLeds = LOW_FIRE_LEDS; // used in fire effect

// Array of fire temperature readings at each simulation cell
byte heat[NUM_LEDS];
byte heat2[NUM_LEDS];

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
     30,   0, 10, 10,
    120,   0,  0,  250,   //blue
    199,   0, 200, 255,
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

CRGBPalette16 firePalettesList [5] = { redFire_gp, blueFire_gp, greenFire_gp, yellowFire_gp, HeatColors_p};

// ----------------------------------------------------------------------------
void setup() {
    DDRB = 0; //all 8-13 are inputs
    PORTB = 0xFF; //all 8-13 pull-up, values will be inverted

    pinMode(INTERRUPT_PIN, INPUT_PULLUP);
    pinMode(INTERRUPT_BTN1, INPUT_PULLUP);
    pinMode(INTERRUPT_BTN2, INPUT_PULLUP);

    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN2>(leds2, NUM_LEDS);

    delay(100); //just don't use LEDs immediately anyway

    FastLED.show(); // Initialize all pixels to 'off'

    random16_add_entropy( random());

    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_PIN), intHandler, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_BTN1), btnHandler1, FALLING);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INTERRUPT_BTN2), btnHandler2, FALLING);
}

// ----------------------------------------------------------------------------
void loop() {
    if (isNewEventOccurred) {
        isNewEventOccurred = 0;
        if (eventId >= EVENT_DRONE_1 && eventId <= EVENT_DRONE_4) { // drone passed
            setNewEffect(EFFECT_RISE_FIRE, 140); // it's a sequence of effects actually
            effectPaletteIndex = eventId;
        } else if (eventId >= EVENT_TONE_1 && eventId <= EVENT_TONE_3) { // start sequence
            setNewEffect(EFFECT_FLASH_DISSOLVE, 1500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[eventId - 10];
        } else if (eventId == EVENT_START_RACE) { // start race
            setNewEffect(EFFECT_FLASH_DISSOLVE, 2500);
            effectBrightness = 200;
            effectHSVColor = countdownCHSVColors[eventId - 10];
        } else if (eventId == EVENT_STOP_RACE) { // race finished, switch to default effect
            fireNumLeds = LOW_FIRE_LEDS;
            setNewEffect(EFFECT_ETERNAL_FIRE, 0);
            effectPaletteIndex = 4; //real fire heatmap
        } else if (eventId == EVENT_PREPARE_RACE) { // prepare to race
            setNewEffect(EFFECT_FIRE_OFF, 1000);
        }
    }

    runCurrentEffect();

    FastLED.show();
}

// ----------------------------------------------------------------------------
void intHandler(void) {
    isNewEventOccurred = 1;
    // portb data is inverted
    eventId = (~PINB) & 0xF;
}

// TODO: remove this handler ((((((DEBUG ONLY))))))
void btnHandler1(void) {
    effectPaletteIndex = ++effectPaletteIndex % 4;
}

// TODO: remove this handler ((((((((DEBUG ONLY))))))))
void btnHandler2(void) {
    setNewEffect(EFFECT_FIRE_OFF, 120);
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
                leds2[i] = color;
            }
            break;
        case WORK_CYCLE_PHASE:
            curBrightness = map(elapsedTime, 0, effectDuration, effectBrightness, 0);
            for(uint8_t i = 0; i < NUM_LEDS; i++) {
                CHSV color = effectHSVColor;
                color.v = qsub8(curBrightness, random8(0, 15));
                leds[i] = color;
                leds2[i] = color;

            }
            break;
        case END_PHASE:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            fill_solid(leds2, NUM_LEDS, CRGB::Black);
            break;
    }
}

void fadeToBlack (uint8_t phase, uint32_t elapsedTime) {
    switch(phase) {
        case START_PHASE:
            break;
        case WORK_CYCLE_PHASE:
            fade_raw(&leds[0], NUM_LEDS, 20);
            fade_raw(&leds2[0], NUM_LEDS, 20);
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
            fill_solid(leds2, NUM_LEDS, CHSV(0, 0, 0));
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
            fill_solid(leds2, NUM_LEDS, CHSV(0, 0, 0));
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

// do the same for another leds strip
    //---------------------
    // Step 1.  Cool down every cell (up to the top) a little
    for( int i = 0; i < fireNumLeds; i++) {
      heat2[i] = qsub8( heat2[i],  random8(0, ((COOLING * 10) / fireNumLeds) + 2));
    }

    // process items above the fire top (to kill flames that remained after big fire)
    for( int i = fireNumLeds; i < NUM_LEDS; i++) {
      heat2[i] = qsub8( heat2[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }

    //---------------------
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= fireNumLeds - 3; k > 0; k--) {
      heat2[k] = (heat2[k - 1] + heat2[k - 2] + heat2[k - 2] ) / 3;
    }

    // process items above the fire top (to kill flames that remained after big fire)
    for( int k= NUM_LEDS - 3; k > fireNumLeds; k--) {
      heat2[k] = (heat2[k - 1] + heat2[k - 2] + heat2[k - 2] ) / 3;
    }

    //---------------------
    // Step 3.  Randomly ignite new 'sparks' of heat2 near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat2[y] = qadd8( heat2[y], random8(160,255) );
    }

    // Step 4.  Map from heat2 cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
        // Scale the heat2 value from 0-255 down to 0-240
        // for best results with color palettes.
        byte colorindex = scale8( heat2[j], 240);
        CRGB color = ColorFromPalette(firePalettesList[effectPaletteIndex], colorindex);

        leds2[j] = color;
    }
}
