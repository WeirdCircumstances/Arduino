/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network.
 *
 * This uses OTAA (Over-the-air activation), where where a DevEUI and
 * application key is configured, which are used in an over-the-air
 * activation procedure where a DevAddr and session keys are
 * assigned/generated for use with all further communication.
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
 * g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
 * violated by this sketch when left running for longer)!

 * To use this sketch, first register your application and device with
 * the things network, to set or generate an AppEUI, DevEUI and AppKey.
 * Multiple devices can use the same AppEUI, but each device has its own
 * DevEUI and AppKey.
 *
 * Do not forget to define the radio type correctly in config.h.
 *
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>

///////////////////////////////////////////////////////////////////////////////////////////

//config from display sketch
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R2, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 22, /* data=*/ 21);   // ESP32 Thing, HW I2C with pin remapping

// assume 4x6 font
#define U8LOG_WIDTH 32
#define U8LOG_HEIGHT 10
uint8_t u8log_buffer[U8LOG_WIDTH*U8LOG_HEIGHT];

U8G2LOG u8g2log;

///////////////////////////////////////////////////////////////////////////////////////////


// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.

static const u1_t PROGMEM APPEUI[8]={ 0x8E, 0x00, 0x01, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
//static const u1_t PROGMEM APPEUI[8]={ 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x01, 0x00, 0x8E };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]={ 0xD9, 0xC2, 0xDA, 0xB8, 0x09, 0x1B, 0x49, 0x00 };
//static const u1_t PROGMEM DEVEUI[8]={ 0x00, 0x49, 0x1B, 0x09, 0xB8, 0xDA, 0xC2, 0xD9 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t PROGMEM APPKEY[16] = { 0x35, 0x3E, 0x46, 0x58, 0x16, 0xB2, 0x9C, 0x09, 0xCF, 0x41, 0xB0, 0x80, 0x60, 0x57, 0x30, 0x9D };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

static uint8_t mydata[] = "Hello, world!";
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 12,
    .dio = {26, 33, LMIC_UNUSED_PIN},
};

void onEvent (ev_t ev) {
    u8g2log.print(os_getTime());
    u8g2log.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            u8g2log.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            u8g2log.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            u8g2log.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            u8g2log.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            u8g2log.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            u8g2log.println(F("EV_JOINED"));

            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            u8g2log.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            u8g2log.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            u8g2log.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            u8g2log.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              u8g2log.println(F("Received ack"));
            if (LMIC.dataLen) {
              u8g2log.println(F("Received "));
              u8g2log.println(LMIC.dataLen);
              u8g2log.println(F(" bytes of payload"));
            }
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            u8g2log.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            u8g2log.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            u8g2log.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            u8g2log.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            u8g2log.println(F("EV_LINK_ALIVE"));
            break;
         default:
            u8g2log.println(F("Unknown event"));
            break;
    }
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        u8g2log.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, mydata, sizeof(mydata)-1, 0);
        u8g2log.println(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {

    /* U8g2 Project: SSD1306 Test Board */
    pinMode(10, OUTPUT);
    pinMode(9, OUTPUT);
    digitalWrite(10, 0);
    digitalWrite(9, 0);  

    u8g2.begin();  
    
    u8g2.setFont(u8g2_font_tom_thumb_4x6_mf);  // set the font for the terminal window
    u8g2log.begin(u8g2, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);
    u8g2log.setLineHeightOffset(0); // set extra space between lines in pixel, this can be negative
    u8g2log.setRedrawMode(0);   // 0: Update screen with newline, 1: Update screen for every char  
      

  
 //   u8g2log.begin(9600);
    u8g2log.println(F("Starting"));

    #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
    #endif

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}
