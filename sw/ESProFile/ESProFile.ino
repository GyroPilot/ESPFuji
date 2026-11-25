// FUJINET MAILBOX EXPERIMENT (Oct 2026, Tim O'Connor with Claude; not published): comment the next line out
// to build the plain ESProFile.  See ESProFile_FujiNet.ino and mailbox_core.h.
#define FUJINET_MAILBOX
// ESPFuji BENCH build: no ProFile emulation; the mailbox requests come from the USB serial port instead, so the
// network side is tested on a PC first.  Define it (or build with -DESPFUJI_BENCH) for the bench, never for a Lisa.
//#define ESPFUJI_BENCH
#ifdef FUJINET_MAILBOX
// The network headers are read here, first of all, so that none of the emulator's #defines leak into them.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#endif

//*************************************************************************************************************
//* ESProFile - A powerful ESP32-based emulator and diagnostic tool for the ProFile family of hard drives.    *
//* By: Alex Anderson-McLeod                                                                                  *
//* Email address: alexelectronicsguy@gmail.com                                                               *
//*************************************************************************************************************

// ******** Changelog ********
// 2/22/2025 - Inlined a few functions to improve performance during ProFile comms.
// 4/19/2026 - Added support for pin definition header files to allow easy customization of ESProFile for different board layouts, and used this to create the LisaFPGA variant of ESProFile. Also cached the ProFile read/write routines to make them fast enough for LisaFPGA's 75MHz DOTCK mode.

#include <Arduino.h> // Include the necessary libraries
#include <SPI.h>
#include "SdFat.h"
#include "sdios.h"
#include "EEPROM.h"

// Include this pin definition file if you've got a standard ESProFile board
#include "PinDefs_ESProFile.h"
// But include this one instead if you're using the ESProFile that's built into the LisaFPGA board
//#include "PinDefs_LisaFPGA.h"

// ESPFuji 1.1.5: the LEDs are switched with register writes only (see ledRouteInit and setLEDColor below).
// An analogWrite call can wait a whole PWM period (1 ms) in the current Arduino core, and setBSY/clearBSY switch
// the LED before they move BSY: every answer to the host came 1 to 2 ms late.
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#define LED_SEL_OFF (SIG_GPIO_OUT_IDX | GPIO_FUNC0_OEN_SEL) // the pin follows its plain GPIO latch, which is held at the LED-off level
#define RED_SEL_REG (GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * red_led)
#define GREEN_SEL_REG (GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * green_led)
uint32_t ledSelRedOn = LED_SEL_OFF; // the output-select value that puts the red LED's PWM on its pin (set by ledRouteInit)
uint32_t ledSelGreenOn = LED_SEL_OFF; // the same for the green LED


// The entire purpose of this file is to pick whether we want to run ESProFile_Diagnostic.ino or ESProFile_Emulator.ino
void setup(){
  pinMode(switch_pin, INPUT); // Set the switch pin to an input
  bool switchState = digitalRead(switch_pin); // Read its state
  pinMode(red_led, OUTPUT); // Set the red LED to an output
  pinMode(green_led, OUTPUT); // And the green LED too
  ledRouteInit(); // ESPFuji 1.1.5: set the LEDs' PWM up once; from here on they are switched with register writes
  Serial.begin(115200); // Start serial comms
  // Make sure that serial doesn't block if nothing is reading from the other end
  // Only run this line if we're running on an S3; the stock ESP32 doesn't have built in USB and so the compiler will give an error on this line
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
      Serial.setTxTimeoutMs(0);
  #endif
#if defined(FUJINET_MAILBOX) && defined(ESPFUJI_BENCH)
  Serial.println("ESPFuji BENCH build: no ProFile emulation, requests from this serial port");
  delay(2000);
  mbBench(); // never returns
#endif
  clearScreen(); // Clear the screen
  setLEDColor(1, 0); // Make the LED red to show that ESProFile is initializing
  if(switchState == 1){ // If the switch is in the diagnostic position, boot into diagnostic mode
    Serial.println("ESProFile is booting into diagnostic mode...");
    delay(2000);
    diagSetup(); // Equivalents of void setup() and void loop() for the diagnostic mode
    diagLoop();
  }
  else{ // Otherwise, boot into emulation mode
    Serial.println("ESProFile is booting into emulation mode...");
    delay(2000);
    emulatorSetup(); // Equivalents of void setup() and void loop() for the emulation mode
    emulatorOuterLoop();
  }
}

// We have a while(1) here because the emulatorLoop will return on errors, in which case we just want to call it again
void emulatorOuterLoop(){
  while(1){
    emulatorLoop();
  }
}

void loop(){
  vTaskDelete(NULL);
  // We don't need to do anything here
}

// The following are helper functions that are used in both ESProFile_Diagnostic.ino and ESProFile_Emulator.ino

// Clears the screen by sending the ANSI escape codes for clearing the screen and moving the cursor to the top-left
void clearScreen(){
  Serial.write(27);
  Serial.print("[2J");
  Serial.write(27);
  Serial.print("[H");
}

// ESPFuji 1.1.5: sets up the PWM of both LEDs once, at their fixed duty cycles, and remembers how each pin is
// routed to its PWM signal.  Must be called again after every pinMode() of the LED pins (pinMode takes the PWM
// away from the pin).  This is the only place where analogWrite is called.
void ledRouteInit(){
  analogWrite(red_led, red_pwm_duty); // Let the Arduino core set the PWM up and route it to the pin
  uint32_t sel = REG_READ(RED_SEL_REG);
  if((sel & GPIO_FUNC0_OUT_SEL_V) != SIG_GPIO_OUT_IDX) ledSelRedOn = sel; // Remember that routing (unless the core left the pin alone)
  REG_WRITE(RED_W1TC_REG, 0b1 << red); // The pin's own latch is the LED-off level (the LEDs are on when the pin is high)
  REG_WRITE(RED_ENABLE_W1TS_REG, 0b1 << red); // And the pin stays an output
  REG_WRITE(RED_SEL_REG, LED_SEL_OFF); // Red off until setLEDColor says otherwise
  analogWrite(green_led, green_pwm_duty); // The same for the green LED
  sel = REG_READ(GREEN_SEL_REG);
  if((sel & GPIO_FUNC0_OUT_SEL_V) != SIG_GPIO_OUT_IDX) ledSelGreenOn = sel;
  REG_WRITE(GREEN_W1TC_REG, 0b1 << green);
  REG_WRITE(GREEN_ENABLE_W1TS_REG, 0b1 << green);
  REG_WRITE(GREEN_SEL_REG, LED_SEL_OFF);
  delay(3); // More than one PWM period, so both duty cycles are in effect before the LEDs are used
}

// Sets the red and green sub-LEDs to the specified values
// ESPFuji 1.1.5: two register writes, no library call.  A LED is on when its pin carries its PWM signal and off
// when the pin follows its plain GPIO latch.  (Green uses PWM since the LED is painfully bright otherwise.)
inline __attribute__((__always_inline__)) void setLEDColor(bool r, bool g){
  REG_WRITE(RED_SEL_REG, r ? ledSelRedOn : LED_SEL_OFF);
  REG_WRITE(GREEN_SEL_REG, g ? ledSelGreenOn : LED_SEL_OFF);
}

// Sets the direction of the parallel bus
inline __attribute__((__always_inline__)) void setParallelDir(bool dir){
  if(dir == 0){ // Set to an input if dir is 0
    REG_WRITE(BUS_ENABLE_W1TC_REG, 0b11111111 << busOffset);
  }
  else if(dir == 1){ // And an output if dir is 1
    REG_WRITE(BUS_ENABLE_W1TS_REG, 0b11111111 << busOffset);
  }
}

// A user-friendly way to send data over the bus in less time-sensitive situations
inline __attribute__((__always_inline__)) void sendData(uint8_t parallelBits){
  REG_WRITE(BUS_W1TS_REG, parallelBits << busOffset); // Write W1TS with the data to set all the bits that need to be set
  REG_WRITE(BUS_W1TC_REG, ((byte)~parallelBits << busOffset)); // Write W1TC with the inverted data to clear all the bits that need to be cleared
}

// A user-friendly way to receive data over the bus in less time-sensitive situations
inline __attribute__((__always_inline__)) uint8_t receiveData(){
  return REG_READ(BUS_IN_REG) >> busOffset; // Return the 8-bit value on the bus
}

// Prints a byte in hex without a space following it
inline __attribute__((__always_inline__)) void printDataNoSpace(uint8_t data){
  Serial.print(data >> 4, HEX); // Print the high nibble
  Serial.print(data & 0x0F, HEX); // And the low nibble
}