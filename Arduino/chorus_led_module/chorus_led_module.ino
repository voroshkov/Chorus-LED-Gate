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


// #define DEBUG

#ifdef DEBUG
    #define DEBUG_CODE(x) do { x } while(0)
#else
    #define DEBUG_CODE(x) do { } while(0)
#endif

#define PORTB_BIT_PIN_INTERRUPT 4 // using PIN12 (bit 4 on PORTB) to generate interrupts on LED driving arduino

#define SERIAL_DATA_DELIMITER '\n'

#include <avr/pgmspace.h>

#define BAUDRATE 115200

// input control byte constants
#define CONTROL_COMMAND_RACE      'R'
#define CONTROL_VALUE_END_RACE      '0'

#define CONTROL_START_COUNTER_PREPARE '1'
#define CONTROL_START_COUNTER_GO '2'

// output id byte constants
#define RESPONSE_LAPTIME        'L'

#define CMD_PREPARE     0x11
#define CMD_COUNTDOWN_3 0x12
#define CMD_COUNTDOWN_2 0x13
#define CMD_COUNTDOWN_1 0x14
#define CMD_START_RACE  0x15
#define CMD_END_RACE    0x16

#define CMD_LAP_DRONE_HI 0x20

#define CMD_SET_COLOR_INDEX_HI 0x30


//----- other globals------------------------------
uint8_t isRaceStarted = 0;

//----- read/write bufs ---------------------------
#define READ_BUFFER_SIZE 20
uint8_t readBuf[READ_BUFFER_SIZE];
uint8_t proxyBuf[READ_BUFFER_SIZE];
uint8_t readBufFilledBytes = 0;
uint8_t proxyBufDataSize = 0;

// ----------------------------------------------------------------------------
#include "fastReadWrite.h"
#include "pinAssignments.h"
#include "sendSerialHex.h"

// ----------------------------------------------------------------------------
void setup() {
    // we'll use pins 8-11 to send 4 bits of data to the LED-driving-arduino, and  pin 12 to
    // let it know that data has been changed.
    // using a simple hack here, but device-dependent, targeting Atmega328 (Uno, Nano, Pro Mini)

    DDRB = 0xFF; // all output
    PORTB = 0xFF; // all high because corresponding module has pull-ups enabled and expects inverted data

    Serial.begin(BAUDRATE);

    delay(200);
    sendCommand(0); // just to warm up and make sure that first command won't be lost
}

// ----------------------------------------------------------------------------
void loop() {
    readSerialDataChunk(); //read passing data and do something in specific cases
}

void sendCommand(uint8_t command) {
    PORTB &= 0xF0; // clear low 4 bits
    PORTB |= 0x0F; // output inverted zero (start of send sequence)
    PORTB ^=(1<<PORTB_BIT_PIN_INTERRUPT); // toggle the value to generate interrupt on receiving controller
    delay(10);

    PORTB &= 0xF0; // clear low 4 bits
    PORTB |= 0xF & (~(command >> 4)); // output inverted high 4 bits
    PORTB ^=(1<<PORTB_BIT_PIN_INTERRUPT); // toggle the value to generate interrupt on receiving controller
    delay(10);

    PORTB &= 0xF0; // clear low 4 bits
    PORTB |= 0xF & (~(command)); // output inverted low 4 bits
    PORTB ^=(1<<PORTB_BIT_PIN_INTERRUPT); // toggle the value to generate interrupt on receiving controller
    delay(10);
}
// ----------------------------------------------------------------------------
void handleSerialRequest(uint8_t *controlData, uint8_t length) {
    uint8_t deviceId = TO_BYTE(controlData[0]);
    uint8_t controlByteCommand = controlData[1];
    uint8_t controlByteValue = controlData[2];

    if(controlByteCommand == CONTROL_COMMAND_RACE) {
        if (controlByteValue == CONTROL_VALUE_END_RACE) {
            isRaceStarted = 0;
            sendCommand(CMD_END_RACE) ; // end race and switch back to default effect
        }
        else {
            isRaceStarted = 1;
            sendCommand(CMD_START_RACE) ; // start race
        }
    }
}

// ----------------------------------------------------------------------------
void handleDedicatedSerialRequest(uint8_t *controlData, uint8_t length) {
    if (controlData[0] == 'P') {
        sendCommand(CMD_PREPARE) ; // prepare to countdown
        return;
    }

    if (controlData[0] == 'C') {
        uint8_t moduleIndex = TO_BYTE(controlData[1]);
        uint8_t colorIndex = TO_BYTE(controlData[2]);
        uint8_t command = CMD_SET_COLOR_INDEX_HI + (colorIndex << 4);
        command |= moduleIndex + 1; // add 1 to index to avoid using zeros in commands (because starting zero is used to identify start of transmission)
        sendCommand(command);
        return;
    }

    uint8_t controlByte = TO_BYTE(controlData[0]);
    if (controlByte > 0 && controlByte < 4) {
        switch(controlByte) {
            case 3:
                sendCommand(CMD_COUNTDOWN_3);
                break;
            case 2:
                sendCommand(CMD_COUNTDOWN_2);
                break;
            case 1:
                sendCommand(CMD_COUNTDOWN_1);
                break;
        }
    }
}

// ----------------------------------------------------------------------------
void handleSerialResponse(uint8_t *responseData, uint8_t length) {
    uint8_t deviceId = TO_BYTE(responseData[0]);
    uint8_t responseCode = responseData[1];

    switch (responseCode) {
        case RESPONSE_LAPTIME:
            sendCommand(CMD_LAP_DRONE_HI | (deviceId + 1)) ; // lap register command with device id
            break;
    }
};

// ----------------------------------------------------------------------------
void readSerialDataChunk () {
    // don't read anything if we have something not sent in proxyBuf
    // if (proxyBufDataSize != 0) return;

    uint8_t availBytes = Serial.available();
    if (availBytes) {
        uint8_t freeBufBytes = READ_BUFFER_SIZE - readBufFilledBytes;

        //reset buffer if we couldn't find delimiter in its contents in prev step
        if (freeBufBytes == 0) {
            readBufFilledBytes = 0;
            freeBufBytes = READ_BUFFER_SIZE;
        }

        //read minimum of "available to read" and "free place in buffer"
        uint8_t canGetBytes = availBytes > freeBufBytes ? freeBufBytes : availBytes;
        Serial.readBytes(&readBuf[readBufFilledBytes], canGetBytes);
        readBufFilledBytes += canGetBytes;
    }

    if (readBufFilledBytes) {
        //try finding a delimiter
        uint8_t foundIdx = 255;
        for (uint8_t i = 0; i < readBufFilledBytes; i++) {
            if (readBuf[i] == SERIAL_DATA_DELIMITER) {
                foundIdx = i;
                break;
            }
        }

        // Yes, in this module we should always pass msg further!
        // uint8_t shouldPassMsgFurther = 1;

        //if delimiter found
        if (foundIdx < READ_BUFFER_SIZE) {
            switch (readBuf[0]) {
                case 'R': //read data from module
                    handleSerialRequest(&readBuf[1], foundIdx);
                    break;
                case 'S': //data sent by prev modules
                    handleSerialResponse(&readBuf[1], foundIdx);
                    break;
                case 'T':
                    handleDedicatedSerialRequest(&readBuf[1], foundIdx);
            }
            // Yes, in this module we should always pass msg further!
            // if (shouldPassMsgFurther) {
                // memmove(proxyBuf, readBuf, foundIdx);
                // proxyBufDataSize = foundIdx;
            // }
            //remove processed portion of data
            memmove(readBuf, &readBuf[foundIdx+1], readBufFilledBytes - foundIdx+1);
            readBufFilledBytes -= foundIdx+1;
        }
    }
}
