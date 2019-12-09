#include <Wire.h>
#include "ntsc_240p.h"
#include "pal_240p.h"
#include "ntsc_feedbackclock.h"
#include "pal_feedbackclock.h"
#include "ntsc_1280x720.h"
#include "ntsc_1280x1024.h"
#include "pal_1280x1024.h"
#include "pal_1280x720.h"
#include "presetMdSection.h"

#include "tv5725.h"
#include "framesync.h"
#include "osd.h"

typedef TV5725<GBS_ADDR> GBS;

#if defined(ESP8266)  // select WeMos D1 R2 & mini in IDE for NodeMCU! (otherwise LED_BUILTIN is mapped to D0 / does not work)
#include <ESP8266WiFi.h>
#include "FS.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "PersWiFiManager.h"
#include <ESP8266mDNS.h>  // mDNS library for finding gbscontrol.local on the local network

//#define HAVE_PINGER_LIBRARY // ESP8266-ping library to aid debugging WiFi issues, install via Arduino library manager
#ifdef HAVE_PINGER_LIBRARY
#include <Pinger.h>
#include <PingerResponse.h>
#endif
// WebSockets library by Markus Sattler
// to install: "Sketch" > "Include Library" > "Manage Libraries ..." > search for "websockets" and install "WebSockets for Arduino (Server + Client)"
#include <WebSocketsServer.h>

const char* ap_ssid = "gbscontrol";
const char* ap_password = "qqqqqqqq";
ESP8266WebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket(81);
PersWiFiManager persWM(server, dnsServer);
#ifdef HAVE_PINGER_LIBRARY
Pinger pinger; // pinger global object to aid debugging WiFi issues
#endif

#define DEBUG_IN_PIN D6 // marked "D12/MISO/D6" (Wemos D1) or D6 (Lolin NodeMCU)
// SCL = D1 (Lolin), D15 (Wemos D1) // ESP8266 Arduino default map: SCL
// SDA = D2 (Lolin), D14 (Wemos D1) // ESP8266 Arduino default map: SDA
#define LEDON \
  pinMode(LED_BUILTIN, OUTPUT); \
  digitalWrite(LED_BUILTIN, LOW)
#define LEDOFF \
  digitalWrite(LED_BUILTIN, HIGH); \
  pinMode(LED_BUILTIN, INPUT)

// fast ESP8266 digitalRead (21 cycles vs 77), *should* work with all possible input pins
// but only "D7" and "D6" have been tested so far
#define digitalRead(x) ((GPIO_REG_READ(GPIO_IN_ADDRESS) >> x) & 1)

#else // Arduino
#define LEDON \
  pinMode(LED_BUILTIN, OUTPUT); \
  digitalWrite(LED_BUILTIN, HIGH)
#define LEDOFF \
  digitalWrite(LED_BUILTIN, LOW); \
  pinMode(LED_BUILTIN, INPUT)

#define DEBUG_IN_PIN 11

// fastest, but non portable (Uno pin 11 = PB3, Mega2560 pin 11 = PB5)
//#define digitalRead(x) bitRead(PINB, 3)
#include "fastpin.h"
#define digitalRead(x) fastRead<x>()

//#define HAVE_BUTTONS
#define INPUT_PIN 9
#define DOWN_PIN 8
#define UP_PIN 7
#define MENU_PIN 6

#endif

//
// Sync locking tunables/magic numbers
//
struct FrameSyncAttrs {
  //static const uint8_t vsyncInPin = VSYNC_IN_PIN;
  static const uint8_t debugInPin = DEBUG_IN_PIN;
  // Sync lock sampling timeout in microseconds
  static const uint32_t timeout = 900000;
  // Sync lock interval in milliseconds
  static const uint32_t lockInterval = 60 * 16; // every 60 frames. good range for this: 30 to 90
  // Sync correction in scanlines to apply when phase lags target
  static const int16_t correction = 2;
  // Target vsync phase offset (output trails input) in degrees
  static const int32_t targetPhase = 90;
  // Number of consistent best htotal results to get in a row before considering it valid
  // ! deprecated when switching htotal measurement source to VDS
  //static const uint8_t htotalStable = 4;
  // Number of samples to average when determining best htotal
  static const uint8_t samples = 1;
};
typedef FrameSyncManager<GBS, FrameSyncAttrs> FrameSync;

struct MenuAttrs {
  static const int8_t shiftDelta = 4;
  static const int8_t scaleDelta = 4;
  static const int16_t vertShiftRange = 300;
  static const int16_t horizShiftRange = 400;
  static const int16_t vertScaleRange = 100;
  static const int16_t horizScaleRange = 130;
  static const int16_t barLength = 100;
};
typedef MenuManager<GBS, MenuAttrs> Menu;

// runTimeOptions holds system variables
struct runTimeOptions {
  unsigned long applyPresetDoneTime;
  uint16_t sourceVLines;
  uint8_t videoStandardInput; // 0 - unknown, 1 - NTSC like, 2 - PAL like, 3 480p NTSC, 4 576p PAL
  uint8_t phaseSP;
  uint8_t phaseADC;
  uint8_t currentLevelSOG;
  uint8_t syncLockFailIgnore;
  uint8_t applyPresetDoneStage;
  uint8_t continousStableCounter;
  boolean isInLowPowerMode;
  boolean clampPositionIsSet;
  boolean coastPositionIsSet;
  boolean inputIsYpBpR;
  boolean syncWatcherEnabled;
  boolean outModePassThroughWithIf;
  boolean printInfos;
  boolean sourceDisconnected;
  boolean webServerEnabled;
  boolean webServerStarted;
  boolean allowUpdatesOTA;
  boolean enableDebugPings;
  boolean webSocketConnected;
  boolean autoBestHtotalEnabled;
  boolean deinterlacerWasTurnedOff;
  boolean forceRetime;
} rtos;
struct runTimeOptions *rto = &rtos;

// userOptions holds user preferences / customizations
struct userOptions {
  uint8_t presetPreference; // 0 - normal, 1 - feedback clock, 2 - customized, 3 - 720p, 4 - 1280x1024
  uint8_t presetGroup;
  uint8_t enableFrameTimeLock;
  uint8_t frameTimeLockMethod;
  uint8_t enableAutoGain;
} uopts;
struct userOptions *uopt = &uopts;

char globalCommand;

#if defined(ESP8266)
// serial mirror class for websocket logs
class SerialMirror : public Stream {
  size_t write(const uint8_t *data, size_t size) {
#if defined(ESP8266)
    //if (rto->webSocketConnected) {
      webSocket.broadcastTXT(data, size); // broadcast is best for cases where contact was lost
    //}
#endif
    Serial.write(data, size);
    //Serial1.write(data, size);
    return size;
  }

  size_t write(uint8_t data) {
#if defined(ESP8266)
    //if (rto->webSocketConnected) {
      webSocket.broadcastTXT(&data);
    //}
#endif
    Serial.write(data);
    //Serial1.write(data);
    return 1;
  }

  int available() {
    return 0;
  }
  int read() {
    return -1;
  }
  int peek() {
    return -1;
  }
  void flush() {       }
};

SerialMirror SerialM;
#else
#define SerialM Serial
#endif

static uint8_t lastSegment = 0xFF;

static inline void writeOneByte(uint8_t slaveRegister, uint8_t value)
{
  writeBytes(slaveRegister, &value, 1);
}

static inline void writeBytes(uint8_t slaveRegister, uint8_t* values, uint8_t numValues)
{
  if (slaveRegister == 0xF0 && numValues == 1) {
    lastSegment = *values;
  }
  else
    GBS::write(lastSegment, slaveRegister, values, numValues);
}

void copyBank(uint8_t* bank, const uint8_t* programArray, uint16_t* index)
{
  for (uint8_t x = 0; x < 16; ++x) {
    bank[x] = pgm_read_byte(programArray + *index);
    (*index)++;
  }
}

void zeroAll()
{
  // turn processing units off first
  writeOneByte(0xF0, 0);
  writeOneByte(0x46, 0x00); // reset controls 1
  writeOneByte(0x47, 0x00); // reset controls 2

  // zero out entire register space
  for (int y = 0; y < 6; y++)
  {
    writeOneByte(0xF0, (uint8_t)y);
    for (int z = 0; z < 16; z++)
    {
      uint8_t bank[16];
      for (int w = 0; w < 16; w++)
      {
        bank[w] = 0;
      }
      writeBytes(z * 16, bank, 16);
    }
  }
}

void loadPresetMdSection() {
  uint16_t index = 0;
  uint8_t bank[16];
  writeOneByte(0xF0, 1);
  for (int j = 6; j <= 7; j++) { // start at 0x60
    copyBank(bank, presetMdSection, &index);
    writeBytes(j * 16, bank, 16);
  }
  bank[0] = pgm_read_byte(presetMdSection + index);
  bank[1] = pgm_read_byte(presetMdSection + index + 1);
  bank[2] = pgm_read_byte(presetMdSection + index + 2);
  bank[3] = pgm_read_byte(presetMdSection + index + 3);
  writeBytes(8 * 16, bank, 4); // MD section ends at 0x83, not 0x90
}

void writeProgramArrayNew(const uint8_t* programArray)
{
  uint16_t index = 0;
  uint8_t bank[16];
  uint8_t y = 0;

  // should only be possible if previously was in RGBHV bypass, then hit a manual preset switch
  if (rto->videoStandardInput == 15) {
    rto->videoStandardInput = 0;
  }
  // programs all valid registers (the register map has holes in it, so it's not straight forward)
  // 'index' keeps track of the current preset data location.
  disableDeinterlacer();

  for (; y < 6; y++)
  {
    writeOneByte(0xF0, (uint8_t)y);
    switch (y) {
    case 0:
      for (int j = 0; j <= 1; j++) { // 2 times
        for (int x = 0; x <= 15; x++) {
          if (j == 0 && x == 4) {
            // keep DAC off for now
            bank[x] = pgm_read_byte(programArray + index) & ~(1 << 0);
          }
          else if (j == 0 && (x == 6 || x == 7)) {
            // keep reset controls active
            bank[x] = 0;
          }
          else if (j == 0 && x == 9) {
            // keep sync output off
            bank[x] = pgm_read_byte(programArray + index) | (1 << 2);
          }
          else {
            // use preset values
            bank[x] = pgm_read_byte(programArray + index);
          }

          index++;
        }
        writeBytes(0x40 + (j * 16), bank, 16);
      }
      copyBank(bank, programArray, &index);
      writeBytes(0x90, bank, 16);
      break;
    case 1:
      for (int j = 0; j <= 2; j++) { // 3 times
        copyBank(bank, programArray, &index);
        writeBytes(j * 16, bank, 16);
      }
      loadPresetMdSection();
      break;
    case 2:
      for (int j = 0; j <= 3; j++) { // 4 times
        copyBank(bank, programArray, &index);
        writeBytes(j * 16, bank, 16);
      }
      break;
    case 3:
      for (int j = 0; j <= 7; j++) { // 8 times
        copyBank(bank, programArray, &index);
        writeBytes(j * 16, bank, 16);
      }
      // blank out VDS PIP registers, otherwise they can end up uninitialized
      for (int x = 0; x <= 15; x++) {
        writeOneByte(0x80 + x, 0x00);
      }
      break;
    case 4:
      for (int j = 0; j <= 5; j++) { // 6 times
        copyBank(bank, programArray, &index);
        writeBytes(j * 16, bank, 16);
      }
      break;
    case 5:
      for (int j = 0; j <= 6; j++) { // 7 times
        for (int x = 0; x <= 15; x++) {
          bank[x] = pgm_read_byte(programArray + index);
          if (index == 386) { // s5_02 bit 6+7 = input selector (only bit 6 is relevant)
            if (rto->inputIsYpBpR)bitClear(bank[x], 6);
            else bitSet(bank[x], 6);
          }
          if (index == 446) { // s5_3e
            bitSet(bank[x], 5); // SP_DIS_SUB_COAST = 1
          }
          if (index == 471) { // s5_57
            bitSet(bank[x], 0); // SP_NO_CLAMP_REG = 1
          }
          index++;
        }
        writeBytes(j * 16, bank, 16);
      }
      break;
    }
  }
}

void setResetParameters() {
  SerialM.println("<reset>");
  rto->videoStandardInput = 0;
  rto->deinterlacerWasTurnedOff = false;
  rto->applyPresetDoneStage = 0;
  rto->sourceVLines = 0;
  rto->sourceDisconnected = true;
  rto->outModePassThroughWithIf = 0; // forget passthrough mode (could be option)
  rto->clampPositionIsSet = 0;
  rto->coastPositionIsSet = 0;
  rto->continousStableCounter = 0;
  rto->isInLowPowerMode = false;
  rto->currentLevelSOG = 4;
  setAndUpdateSogLevel(rto->currentLevelSOG);
  GBS::RESET_CONTROL_0x46::write(0x00); // all units off
  GBS::RESET_CONTROL_0x47::write(0x00);
  GBS::GPIO_CONTROL_00::write(0xff); // all GPIO pins regular GPIO
  GBS::GPIO_CONTROL_01::write(0x00); // all GPIO outputs disabled
  GBS::DAC_RGBS_PWDNZ::write(0); // disable DAC (output)
  GBS::PLL648_CONTROL_01::write(0x00); // VCLK(1/2/4) display clock // needs valid for debug bus
  GBS::IF_SEL_ADC_SYNC::write(1); // ! 1_28
  GBS::PLLAD_VCORST::write(1); // reset = 1
  GBS::PLL_ADS::write(1); // When = 1, input clock is from ADC ( otherwise, from unconnected clock at pin 40 )
  GBS::PLL_CKIS::write(0); // PLL use OSC clock
  GBS::PLL_MS::write(2); // fb memory clock can go lower power
  GBS::PAD_CONTROL_00_0x48::write(0x2b); //disable digital inputs, enable debug out pin
  GBS::PAD_CONTROL_01_0x49::write(0x1f); //pad control pull down/up transistors on
  GBS::INTERRUPT_CONTROL_01::write(0xff); // enable interrupts
  // adc for sog detection
  GBS::ADC_INPUT_SEL::write(1); // 1 = RGBS / RGBHV adc data input
  GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input ( 5_20 bit 3 )
  //GBS::ADC_TR_RSEL::write(2); // 5_04 // ADC_TR_RSEL = 2 test
  //GBS::ADC_TR_ISEL::write(0); // leave current at default
  GBS::ADC_TEST::write(2); // 5_0c bit 2 // should work now
  GBS::SP_NO_CLAMP_REG::write(1);
  GBS::ADC_SOGEN::write(1);
  GBS::ADC_POWDZ::write(1); // ADC on
  GBS::PLLAD_ICP::write(0); // lowest charge pump current
  GBS::PLLAD_FS::write(0); // low gain (have to deal with cold and warm startups)
  GBS::PLLAD_5_16::write(0x1f); // this maybe needs to be the sog detection default
  GBS::PLLAD_MD::write(0x700);
  resetPLL(); // cycles PLL648
  resetPLLAD(); // same for PLLAD
  GBS::PLL_VCORST::write(1); // reset on
  GBS::PLLAD_CONTROL_00_5x11::write(0x01); // reset on
  enableDebugPort();
  GBS::RESET_CONTROL_0x47::write(0x16);
  GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
  GBS::INTERRUPT_CONTROL_00::write(0x00);
  GBS::RESET_CONTROL_0x47::write(0x16); // decimation off
  GBS::PAD_SYNC_OUT_ENZ::write(0); // sync output enabled, will be low (HC125 fix)
}

void applyYuvPatches() {
  GBS::ADC_AUTO_OFST_EN::write(1); 
  GBS::IF_AUTO_OFST_EN::write(1);

  GBS::ADC_RYSEL_R::write(1); // midlevel clamp red
  GBS::ADC_RYSEL_B::write(1); // midlevel clamp blue
  GBS::IF_MATRIX_BYPS::write(1);
  // colors
  GBS::VDS_Y_GAIN::write(0x80); // 0x80 = 0
  GBS::VDS_VCOS_GAIN::write(0x28); // red
  GBS::VDS_UCOS_GAIN::write(0x1c); // blue
  GBS::VDS_Y_OFST::write(0x00); // 0 3_3a
  GBS::VDS_U_OFST::write(0x00); // 0 3_3b
  GBS::VDS_V_OFST::write(0x00); // 0 3_3c
}

void applyRGBPatches() {
  GBS::ADC_AUTO_OFST_EN::write(0); 
  GBS::IF_AUTO_OFST_EN::write(0);
  GBS::ADC_RYSEL_R::write(0); // gnd clamp red
  GBS::ADC_RYSEL_B::write(0); // gnd clamp blue
  GBS::IF_MATRIX_BYPS::write(0);
  // colors
  GBS::VDS_Y_GAIN::write(0x80); // 0x80 = 0
  GBS::VDS_VCOS_GAIN::write(0x28); // red
  GBS::VDS_UCOS_GAIN::write(0x1c); // blue
  GBS::VDS_USIN_GAIN::write(0x00); // 3_38
  GBS::VDS_VSIN_GAIN::write(0x00); // 3_39
  GBS::VDS_Y_OFST::write(0x00); // 3_3a
  GBS::VDS_U_OFST::write(0x00); // 3_3b
  GBS::VDS_V_OFST::write(0x00); // 3_3c
}

void setAdcParametersGainAndOffset() {
  if (rto->inputIsYpBpR == 1) {
    GBS::ADC_ROFCTRL::write(0x3f); // R and B ADC channels are less offset
    GBS::ADC_GOFCTRL::write(0x43); // calibrate with VP2 in 480p mode
    GBS::ADC_BOFCTRL::write(0x3f);
  }
  else {
    GBS::ADC_ROFCTRL::write(0x3f); // R and B ADC channels seem to be offset from G in hardware
    GBS::ADC_GOFCTRL::write(0x43);
    GBS::ADC_BOFCTRL::write(0x3f);
  }
  GBS::ADC_RGCTRL::write(0x7b); // ADC_TR_RSEL = 2 test
  GBS::ADC_GGCTRL::write(0x7b);
  GBS::ADC_BGCTRL::write(0x7b);
}

void setSpParameters() {
  writeOneByte(0xF0, 5);
  GBS::SP_SOG_P_ATO::write(1); // 5_20 enable sog auto polarity
  GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input 0 ( 5_20 bit 3 )
  // H active detect control
  writeOneByte(0x21, 0x20); // SP_SYNC_TGL_THD    H Sync toggle times threshold  0x20 // ! lower than 5_33, 4 ticks (ie 20 < 24)  !
  writeOneByte(0x22, 0x10); // SP_L_DLT_REG       Sync pulse width different threshold (little than this as equal). // 7
  writeOneByte(0x23, 0x00); // UNDOCUMENTED       range from 0x00 to at least 0x1d
  writeOneByte(0x24, 0x0b); // SP_T_DLT_REG       H total width different threshold rgbhv: b // range from 0x02 upwards
  writeOneByte(0x25, 0x00); // SP_T_DLT_REG
  writeOneByte(0x26, 0x08); // SP_SYNC_PD_THD     H sync pulse width threshold // from 0(?) to 0x50 // in yuv 720p range only to 0x0a!
  writeOneByte(0x27, 0x00); // SP_SYNC_PD_THD
  writeOneByte(0x2a, 0x03); // SP_PRD_EQ_THD     ! How many continue legal line as valid // effect on MD recovery after sync loss
  // V active detect control
  // these 4 have no effect currently test string:  s5s2ds34 s5s2es24 s5s2fs16 s5s31s84   |   s5s2ds02 s5s2es04 s5s2fs02 s5s31s04
  writeOneByte(0x2d, 0x02); // SP_VSYNC_TGL_THD   V sync toggle times threshold // at 5 starts to drop many 0_16 vsync events
  writeOneByte(0x2e, 0x00); // SP_SYNC_WIDTH_DTHD V sync pulse width threshod
  writeOneByte(0x2f, 0x02); // SP_V_PRD_EQ_THD    How many continue legal v sync as valid // at 4 starts to drop 0_16 vsync events
  writeOneByte(0x31, 0x2f); // SP_VT_DLT_REG      V total different threshold
  // Timer value control
  writeOneByte(0x33, 0x2e); // SP_H_TIMER_VAL    ! H timer value for h detect (was 0x28) // coupled with 5_2a and 5_21 // test bus 5_63 to 0x25 and scope dbg pin
  writeOneByte(0x34, 0x05); // SP_V_TIMER_VAL     V timer for V detect // affects 0_16 vsactive
  // Sync separation control
  writeOneByte(0x35, 0x25); // SP_DLT_REG [7:0]   start at higher value, update later // SP test: a0
  writeOneByte(0x36, 0x00); // SP_DLT_REG [11:8]

  if (rto->videoStandardInput == 0) {
    writeOneByte(0x37, 0x06); // need to detect tri level sync (max 0x0c in a test)
  }
  else if (rto->videoStandardInput <= 2) {
    writeOneByte(0x37, 0x10); // SP_H_PULSE_IGNOR // SP test (snes needs 2+, MD in MS mode needs 0x0e)
  }
  else {
    writeOneByte(0x37, 0x04);
  }

  GBS::SP_PRE_COAST::write(6); // SP test: 9
  GBS::SP_POST_COAST::write(16); // SP test: 9

  writeOneByte(0x3a, 0x03); // was 0x0a // range depends on source vtiming, from 0x03 to xxx, some good effect at lower levels

  GBS::SP_SDCS_VSST_REG_H::write(0);
  GBS::SP_SDCS_VSSP_REG_H::write(0);
  GBS::SP_SDCS_VSST_REG_L::write(0x02); // 5_3f // 0 and 1 not good in passthrough modes, assume same for regular
  GBS::SP_SDCS_VSSP_REG_L::write(0x0b); // 5_40 0x0b=11
  if (rto->videoStandardInput == 1 || rto->videoStandardInput == 6) {
    GBS::SP_SDCS_VSSP_REG_L::write(0x0c); // S5_40 (interlaced)
  }
  GBS::SP_CS_HS_ST::write(0x00);
  GBS::SP_CS_HS_SP::write(0x08); // was 0x05, 720p source needs 0x08

  writeOneByte(0x49, 0x04); // 0x04 rgbhv: 20
  writeOneByte(0x4a, 0x00); // 0xc0
  writeOneByte(0x4b, 0x34); // 0x34 rgbhv: 50
  writeOneByte(0x4c, 0x00); // 0xc0

  writeOneByte(0x51, 0x02); // 0x00 rgbhv: 2
  writeOneByte(0x52, 0x00); // 0xc0
  writeOneByte(0x53, 0x06); // 0x05 rgbhv: 6
  writeOneByte(0x54, 0x00); // 0xc0

  if (rto->videoStandardInput != 15) {
    writeOneByte(0x3e, 0x00); // SP sub coast on / with ofw protect disabled; snes 239 to normal rapid switches
    GBS::SP_CLAMP_MANUAL::write(0); // 0 = automatic on/off possible
    GBS::SP_CLP_SRC_SEL::write(1); // clamp source 1: pixel clock, 0: 27mhz
    GBS::SP_SOG_MODE::write(1);
    //GBS::SP_NO_CLAMP_REG::write(0); // yuv inputs need this
    GBS::SP_H_CST_SP::write(0x38); //snes minimum: inHlength -12 (only required in 239 mode)
    GBS::SP_H_CST_ST::write(0x38); // low but not near 0 (typical: 0x6a)
    GBS::SP_DIS_SUB_COAST::write(1); // auto coast initially off (hsync coast)
    GBS::SP_HCST_AUTO_EN::write(0);
    //GBS::SP_HS2PLL_INV_REG::write(0); // rgbhv is improved with = 1, but ADC sync might demand 0
  }

  writeOneByte(0x58, 0x05); //rgbhv: 0
  writeOneByte(0x59, 0x00); //rgbhv: 0
  writeOneByte(0x5a, 0x01); //rgbhv: 0 // was 0x05 but 480p ps2 doesnt like it
  writeOneByte(0x5b, 0x00); //rgbhv: 0
  writeOneByte(0x5c, 0x03); //rgbhv: 0
  writeOneByte(0x5d, 0x02); //rgbhv: 0 // range: 0 - 0x20 (how long should clamp stay off)
}

// Sync detect resolution: 5bits; comparator voltage range 10mv~320mv.
// -> 10mV per step; if cables and source are to standard (level 6 = 60mV)
void setAndUpdateSogLevel(uint8_t level) {
  GBS::ADC_SOGCTRL::write(level);
  rto->currentLevelSOG = level;
}

// in operation: t5t04t1 for 10% lower power on ADC
// also: s0s40s1c for 5% (lower memclock of 108mhz)
// for some reason: t0t45t2 t0t45t4 (enable SDAC, output max voltage) 5% lower  done in presets
// t0t4ft4 clock out should be off
// s4s01s20 (was 30) faster latency // unstable at 108mhz
// both phase controls off saves some power 506ma > 493ma
// oversample ratio can save 10% at 1x
// t3t24t3 VDS_TAP6_BYPS on can save 2%

// Generally, the ADC has to stay enabled to perform SOG separation and thus "see" a source.
// It is possible to run in low power mode.
void goLowPowerWithInputDetection() {
  SerialM.println("low power");
  zeroAll();
  setResetParameters(); // includes rto->videoStandardInput = 0
  loadPresetMdSection(); // fills 1_60 to 1_83
  setAdcParametersGainAndOffset();
  setSpParameters();
  delay(300);
  GBS::PAD_SYNC_OUT_ENZ::write(1);
  GBS::PAD_SYNC_OUT_ENZ::write(0); // HC125 floating inputs fix
  rto->isInLowPowerMode = true;
  LEDOFF;
}

// findbestphase: this one actually works. it only served to show that the SP is best working
// at level 15 for SOG sync (RGBS and YUV)
void optimizePhaseSP() {
  if (rto->videoStandardInput == 15 || GBS::SP_SOG_MODE::read() != 1 || 
    rto->outModePassThroughWithIf) 
  {
    return;
  }
  uint16_t maxFound = 0;
  uint8_t idealPhase = 0;
  uint8_t debugRegSp = GBS::TEST_BUS_SP_SEL::read();
  uint8_t debugRegBus = GBS::TEST_BUS_SEL::read();
  GBS::TEST_BUS_SP_SEL::write(0x0b); // # 5 cs_sep module
  GBS::TEST_BUS_SEL::write(0xa);
  GBS::PA_SP_LOCKOFF::write(1);
  rto->phaseSP = 0;

  for (uint8_t i = 0; i < 16; i++) {
    GBS::PLLAD_BPS::write(0); GBS::PLLAD_BPS::write(1);
    GBS::SFTRST_SYNC_RSTZ::write(0); // SP reset
    delay(1); GBS::SFTRST_SYNC_RSTZ::write(1);
    delay(1); setPhaseSP(); 
    delay(4);
    uint16_t result = GBS::TEST_BUS::read() & 0x7ff; // highest byte seems garbage
    static uint16_t lastResult = result;
    if (result > maxFound && (lastResult > 10)) {
      maxFound = result;
      idealPhase = rto->phaseSP;
    }
    /*Serial.print(rto->phaseSP); 
    if (result < 10) { Serial.print(" :"); }
    else { Serial.print(":"); }
    Serial.println(result);*/
    rto->phaseSP += 2;
    rto->phaseSP &= 0x1f;
    lastResult = result;
  }
  SerialM.print("phaseSP: "); SerialM.println(idealPhase);

  GBS::PLLAD_BPS::write(0);
  GBS::PA_SP_LOCKOFF::write(0);
  GBS::TEST_BUS_SP_SEL::write(debugRegSp);
  GBS::TEST_BUS_SEL::write(debugRegBus);
  //delay(150); // recover from resets
  rto->phaseSP = idealPhase;
  setPhaseSP();
}

void optimizeSogLevel() {
  if (rto->videoStandardInput == 15 || GBS::SP_SOG_MODE::read() != 1) return;
  uint8_t jitter = 10;
  GBS::PLLAD_PDZ::write(1);
  uint8_t debugRegSp = GBS::TEST_BUS_SP_SEL::read();
  uint8_t debugRegBus = GBS::TEST_BUS_SEL::read();
  GBS::TEST_BUS_SP_SEL::write(0x5b); // # 5 cs_sep module
  // use PLLAD_BPS=1 and 5_63=0x0b instead! also 5_19 bit 6 off (lock SP) does smth
  GBS::PLLAD_PDZ::write(1);
  GBS::TEST_BUS_SEL::write(0xa);

  rto->currentLevelSOG = 31;
  setAndUpdateSogLevel(rto->currentLevelSOG);
  delay(80);
  while (1) {
    jitter = 0;
    for (uint8_t i = 0; i < 10; i++) {
      uint16_t test1 = GBS::TEST_BUS::read() & 0x07ff;
      delay(random(2, 6)); // random(inclusive, exclusive));
      uint16_t test2 = GBS::TEST_BUS::read() & 0x07ff;
      if (((test1 & 0x00ff) == (test2 & 0x00ff)) && ((test1 > 0x00d0) && (test1 < 0x0180))) {
        jitter++;
        //Serial.print("1: ");Serial.print(test1, HEX);Serial.print(" 2: ");Serial.println(test2, HEX);
      }
    }
    if (jitter >= 10) { break; } // found
    SerialM.print("+");
    setAndUpdateSogLevel(rto->currentLevelSOG);
    delay(10);
    if (rto->currentLevelSOG >= 1) rto->currentLevelSOG -= 1;
    else {
      break;
    }
  }

  if (rto->currentLevelSOG < 31 && rto->currentLevelSOG > 16) { rto->currentLevelSOG = 9; }
  else if (rto->currentLevelSOG >= 12) { rto->currentLevelSOG -= 7; }
  else if (rto->currentLevelSOG >= 3) { rto->currentLevelSOG -= 2; }
  SerialM.print("\nsog level: "); SerialM.println(rto->currentLevelSOG);
  setAndUpdateSogLevel(rto->currentLevelSOG);

  GBS::TEST_BUS_SP_SEL::write(debugRegSp);
  GBS::TEST_BUS_SEL::write(debugRegBus);
}

// GBS boards have 2 potential sync sources:
// - RCA connectors
// - VGA input / 5 pin RGBS header / 8 pin VGA header (all 3 are shared electrically)
// This routine looks for sync on the currently active input. If it finds it, the input is returned.
// If it doesn't find sync, it switches the input and returns 0, so that an active input will be found eventually.
// This is done this way to not block the control MCU with active searching for sync.
uint8_t detectAndSwitchToActiveInput() { // if any
  uint8_t currentInput = GBS::ADC_INPUT_SEL::read();

  unsigned long timeout = millis();
  while (millis() - timeout < 450) {
    delay(10); // in case the input switch needs time to settle (it didn't in a short test though)
    uint8_t videoMode = getVideoMode();

    if (GBS::TEST_BUS_2F::read() != 0 || videoMode > 0) {
      currentInput = GBS::ADC_INPUT_SEL::read();
      videoMode = getVideoMode();
      SerialM.print("found: "); SerialM.print(GBS::TEST_BUS_2F::read()); SerialM.print(" getVideoMode: "); SerialM.print(getVideoMode());
      SerialM.print(" input: "); SerialM.println(currentInput);
      if (currentInput == 1) { // RGBS or RGBHV
        unsigned long timeOutStart = millis();
        while ((millis() - timeOutStart) < 300) {
          delay(8);
          if (getVideoMode() > 0) {
            SerialM.println(" RGBS");
            return 1;
          }
        }
        // is it RGBHV instead?
        GBS::SP_SOG_MODE::write(0);
        boolean vsyncActive = 0;
        timeOutStart = millis();
        while (!vsyncActive && millis() - timeOutStart < 400) {
          vsyncActive = GBS::STATUS_SYNC_PROC_VSACT::read();
          delay(1); // wifi stack
        }
        if (vsyncActive) {
          SerialM.println("VS active");
          boolean hsyncActive = 0;
          timeOutStart = millis();
          while (!hsyncActive && millis() - timeOutStart < 400) {
            hsyncActive = GBS::STATUS_SYNC_PROC_HSACT::read();
            delay(1); // wifi stack
          }
          if (hsyncActive) {
            SerialM.println("HS active");
            rto->inputIsYpBpR = 0;
            rto->sourceDisconnected = false;
            rto->videoStandardInput = 15;
            applyPresets(rto->videoStandardInput); // exception: apply preset here, not later in syncwatcher
            delay(100);
            return 3;
          }
          else {
            SerialM.println("but no HS!");
          }
        }

        GBS::SP_SOG_MODE::write(1);
        resetSyncProcessor();
        resetModeDetect(); // there was some signal but we lost it. MD is stuck anyway, so reset
        delay(40);
      }
      else if (currentInput == 0) { // YUV
        SerialM.println(" YUV");
        delay(200); // give yuv a chance to recognize the video mode as well (not just activity)
        unsigned long timeOutStart = millis();
        while ((millis() - timeOutStart) < 600) {
          delay(40);
          if (getVideoMode() > 0) {
            return 2;
          }
        }
      }
      SerialM.println(" lost..");
    }
    
    GBS::ADC_INPUT_SEL::write(!currentInput); // can only be 1 or 0
  }

  return 0;
}

uint8_t inputAndSyncDetect() {
  uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
  uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(0xa); delay(1);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(0x0f); delay(1);
  }

  uint8_t syncFound = detectAndSwitchToActiveInput();

  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(debug_backup);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
  }

  if (syncFound == 0) {
    if (!getSyncPresent()) {
      if (rto->isInLowPowerMode == false) {
        SerialM.println("\n no sync found");
        rto->sourceDisconnected = true;
        rto->videoStandardInput = 0;
        // reset to base settings, then go to low power
        GBS::SP_SOG_MODE::write(1);
        goLowPowerWithInputDetection();
        rto->isInLowPowerMode = true;
      }
    }
    return 0;
  }
  else if (syncFound == 1) { // input is RGBS
    rto->inputIsYpBpR = 0;
    rto->sourceDisconnected = false;
    rto->isInLowPowerMode = false;
    applyRGBPatches();
    LEDON;
    return 1;
  }
  else if (syncFound == 2) {
    rto->inputIsYpBpR = 1;
    rto->sourceDisconnected = false;
    rto->isInLowPowerMode = false;
    applyYuvPatches();
    LEDON;
    return 2;
  }
  else if (syncFound == 3) { // input is RGBHV
    //already applied
    rto->isInLowPowerMode = false;
    rto->inputIsYpBpR = 0;
    rto->sourceDisconnected = false;
    rto->videoStandardInput = 15;
    LEDON;
    return 3;
  }

  return 0;
}

uint8_t getSingleByteFromPreset(const uint8_t* programArray, unsigned int offset) {
  return pgm_read_byte(programArray + offset);
}

static inline void readFromRegister(uint8_t reg, int bytesToRead, uint8_t* output)
{
  return GBS::read(lastSegment, reg, output, bytesToRead);
}

void printReg(uint8_t seg, uint8_t reg) {
  uint8_t readout;
  readFromRegister(reg, 1, &readout);
  // didn't think this HEX trick would work, but it does! (?)
  SerialM.print("0x"); SerialM.print(readout, HEX); SerialM.print(", // s"); SerialM.print(seg); SerialM.print("_"); SerialM.println(reg, HEX);
  // old:
  //SerialM.print(readout); SerialM.print(", // s"); SerialM.print(seg); SerialM.print("_"); SerialM.println(reg, HEX);
}

// dumps the current chip configuration in a format that's ready to use as new preset :)
void dumpRegisters(byte segment)
{
  if (segment > 5) return;
  writeOneByte(0xF0, segment);

  switch (segment) {
  case 0:
    for (int x = 0x40; x <= 0x5F; x++) {
      printReg(0, x);
    }
    for (int x = 0x90; x <= 0x9F; x++) {
      printReg(0, x);
    }
    break;
  case 1:
    for (int x = 0x0; x <= 0x2F; x++) {
      printReg(1, x);
    }
    break;
  case 2:
    for (int x = 0x0; x <= 0x3F; x++) {
      printReg(2, x);
    }
    break;
  case 3:
    for (int x = 0x0; x <= 0x7F; x++) {
      printReg(3, x);
    }
    break;
  case 4:
    for (int x = 0x0; x <= 0x5F; x++) {
      printReg(4, x);
    }
    break;
  case 5:
    for (int x = 0x0; x <= 0x6F; x++) {
      printReg(5, x);
    }
    break;
  }
}

void resetPLLAD() {
  GBS::PLLAD_VCORST::write(1);
  delay(1);
  GBS::PLLAD_VCORST::write(0);
  delay(1);
  latchPLLAD();
}

void latchPLLAD() {
  GBS::PLLAD_LAT::write(0);
  delay(1);
  GBS::PLLAD_LAT::write(1);
}

void resetPLL() {
  GBS::PAD_OSC_CNTRL::write(2); // crystal drive
  GBS::PLL_LEN::write(0);
  GBS::PLL_VCORST::write(1);
  delay(1);
  GBS::PLL_VCORST::write(0);
  delay(1);
  GBS::PLL_LEN::write(1);
}

void ResetSDRAM() {
  GBS::SDRAM_RESET_CONTROL::write(0x07); delay(2); // enable "Software Control SDRAM Idle Period" 0x00 for off
  GBS::SDRAM_RESET_SIGNAL::write(1); //delay(4);
  GBS::SDRAM_START_INITIAL_CYCLE::write(1); delay(4);
  GBS::SDRAM_RESET_SIGNAL::write(0); //delay(2);
  GBS::SDRAM_START_INITIAL_CYCLE::write(0); //delay(2);
}

// soft reset cycle
// This restarts all chip units, which is sometimes required when important config bits are changed.
void resetDigital() {
  if (rto->outModePassThroughWithIf) { // if in bypass, treat differently
    GBS::RESET_CONTROL_0x46::write(0);
    GBS::RESET_CONTROL_0x47::write(0);
    if (rto->inputIsYpBpR) {
      GBS::RESET_CONTROL_0x46::write(0x41); // VDS + IF on
    }
    //else leave 0_46 at 0 (all off)
    GBS::RESET_CONTROL_0x47::write(0x17);
    return;
  }

  if (GBS::SFTRST_VDS_RSTZ::read() == 1) { // if VDS enabled
    GBS::RESET_CONTROL_0x46::write(0x40); // then keep it enabled
  }
  else {
    GBS::RESET_CONTROL_0x46::write(0x00);
  }
  GBS::RESET_CONTROL_0x47::write(0x00);
  // enable
  if (rto->videoStandardInput != 15) GBS::RESET_CONTROL_0x46::write(0x7f);
  GBS::RESET_CONTROL_0x47::write(0x17);  // all on except HD bypass
  delay(1);
  ResetSDRAM();
  resetPLL();
}

void resetSyncProcessor() {
  GBS::SFTRST_SYNC_RSTZ::write(0);
  delay(1);
  GBS::SFTRST_SYNC_RSTZ::write(1);
}

void resetModeDetect() {
  GBS::SFTRST_MODE_RSTZ::write(0);
  delay(1); // needed
  GBS::SFTRST_MODE_RSTZ::write(1);
}

void shiftHorizontal(uint16_t amountToAdd, bool subtracting) {
  typedef GBS::Tie<GBS::VDS_HB_ST, GBS::VDS_HB_SP> Regs;
  uint16_t hrst = GBS::VDS_HSYNC_RST::read();
  uint16_t hbst = 0, hbsp = 0;

  Regs::read(hbst, hbsp);

  // Perform the addition/subtraction
  if (subtracting) {
    hbst -= amountToAdd;
    hbsp -= amountToAdd;
  }
  else {
    hbst += amountToAdd;
    hbsp += amountToAdd;
  }

  // handle the case where hbst or hbsp have been decremented below 0
  if (hbst & 0x8000) {
    hbst = hrst % 2 == 1 ? (hrst + hbst) + 1 : (hrst + hbst);
  }
  if (hbsp & 0x8000) {
    hbsp = hrst % 2 == 1 ? (hrst + hbsp) + 1 : (hrst + hbsp);
  }

  // handle the case where hbst or hbsp have been incremented above hrst
  if (hbst > hrst) {
    hbst = hrst % 2 == 1 ? (hbst - hrst) - 1 : (hbst - hrst);
  }
  if (hbsp > hrst) {
    hbsp = hrst % 2 == 1 ? (hbsp - hrst) - 1 : (hbsp - hrst);
  }

  Regs::write(hbst, hbsp);
}

void shiftHorizontalLeft() {
  shiftHorizontal(4, true);
}

void shiftHorizontalRight() {
  shiftHorizontal(4, false);
}

void shiftHorizontalLeftIF(uint8_t amount) {
  uint16_t IF_HB_ST2 = GBS::IF_HB_ST2::read() + amount;
  uint16_t IF_HB_SP2 = GBS::IF_HB_SP2::read() + amount;
  if (rto->videoStandardInput <= 2) {
    GBS::IF_HSYNC_RST::write(GBS::PLLAD_MD::read() / 2); // input line length from pll div
  }
  else if (rto->videoStandardInput <= 7) {
    GBS::IF_HSYNC_RST::write(GBS::PLLAD_MD::read());
  }
  uint16_t IF_HSYNC_RST = GBS::IF_HSYNC_RST::read();

  GBS::IF_LINE_SP::write(IF_HSYNC_RST + 1);

  // start
  if (IF_HB_ST2 < IF_HSYNC_RST) GBS::IF_HB_ST2::write(IF_HB_ST2);
  else {
    GBS::IF_HB_ST2::write(IF_HB_ST2 - IF_HSYNC_RST);
  }
  //SerialM.print("IF_HB_ST2:  "); SerialM.println(GBS::IF_HB_ST2::read());

  // stop
  if (IF_HB_SP2 < IF_HSYNC_RST) GBS::IF_HB_SP2::write(IF_HB_SP2);
  else {
    GBS::IF_HB_SP2::write((IF_HB_SP2 - IF_HSYNC_RST) + 1);
  }
  //SerialM.print("IF_HB_SP2:  "); SerialM.println(GBS::IF_HB_SP2::read());
}

void shiftHorizontalRightIF(uint8_t amount) {
  int16_t IF_HB_ST2 = GBS::IF_HB_ST2::read() - amount;
  int16_t IF_HB_SP2 = GBS::IF_HB_SP2::read() - amount;
  if (rto->videoStandardInput <= 2) {
    GBS::IF_HSYNC_RST::write(GBS::PLLAD_MD::read() / 2); // input line length from pll div
  }
  else if (rto->videoStandardInput <= 7) {
    GBS::IF_HSYNC_RST::write(GBS::PLLAD_MD::read());
  }
  int16_t IF_HSYNC_RST = GBS::IF_HSYNC_RST::read();

  GBS::IF_LINE_SP::write(IF_HSYNC_RST + 1);

  if (IF_HB_ST2 > 0) GBS::IF_HB_ST2::write(IF_HB_ST2);
  else {
    GBS::IF_HB_ST2::write(IF_HSYNC_RST - 1);
  }
  //SerialM.print("IF_HB_ST2:  "); SerialM.println(GBS::IF_HB_ST2::read());

  if (IF_HB_SP2 > 0) GBS::IF_HB_SP2::write(IF_HB_SP2);
  else {
    GBS::IF_HB_SP2::write(IF_HSYNC_RST - 1);
    //GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() - 2);
  }
  //SerialM.print("IF_HB_SP2:  "); SerialM.println(GBS::IF_HB_SP2::read());
}

void scaleHorizontal(uint16_t amountToAdd, bool subtracting) {
  uint16_t hscale = GBS::VDS_HSCALE::read();

  if (subtracting && (hscale - amountToAdd > 0)) {
    hscale -= amountToAdd;
  }
  else if (hscale + amountToAdd <= 1023) {
    hscale += amountToAdd;
  }

  SerialM.print("Scale Hor: "); SerialM.println(hscale);
  GBS::VDS_HSCALE::write(hscale);
}

void scaleHorizontalSmaller() {
  scaleHorizontal(1, false);
}

void scaleHorizontalLarger() {
  scaleHorizontal(1, true);
}

void moveHS(uint16_t amountToAdd, bool subtracting) {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0a, 1, &low);
  readFromRegister(0x0b, 1, &high);
  newST = ((((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0b, 1, &low);
  readFromRegister(0x0c, 1, &high);
  newSP = ((((uint16_t)high) & 0x00ff) << 4) | ((((uint16_t)low) & 0x00f0) >> 4);

  if (subtracting) {
    newST -= amountToAdd;
    newSP -= amountToAdd;
  }
  else {
    newST += amountToAdd;
    newSP += amountToAdd;
  }
  //SerialM.print("HSST: "); SerialM.print(newST);
  //SerialM.print(" HSSP: "); SerialM.println(newSP);

  writeOneByte(0x0a, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0b, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)));
  writeOneByte(0x0c, (uint8_t)((newSP & 0x0ff0) >> 4));
}

void moveVS(uint16_t amountToAdd, bool subtracting) {
  uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
  uint16_t VDS_DIS_VB_ST = GBS::VDS_DIS_VB_ST::read();
  uint16_t newVDS_VS_ST = GBS::VDS_VS_ST::read();
  uint16_t newVDS_VS_SP = GBS::VDS_VS_SP::read();

  if (subtracting) {
    if ((newVDS_VS_ST - amountToAdd) > VDS_DIS_VB_ST) {
      newVDS_VS_ST -= amountToAdd;
      newVDS_VS_SP -= amountToAdd;
    }
    else SerialM.println("limit");
  }
  else {
    if ((newVDS_VS_SP + amountToAdd) < vtotal) {
      newVDS_VS_ST += amountToAdd;
      newVDS_VS_SP += amountToAdd;
    }
    else SerialM.println("limit");
  }
  //SerialM.print("VSST: "); SerialM.print(newVDS_VS_ST);
  //SerialM.print(" VSSP: "); SerialM.println(newVDS_VS_SP);

  GBS::VDS_VS_ST::write(newVDS_VS_ST);
  GBS::VDS_VS_SP::write(newVDS_VS_SP);
}

void invertHS() {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0a, 1, &low);
  readFromRegister(0x0b, 1, &high);
  newST = ((((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0b, 1, &low);
  readFromRegister(0x0c, 1, &high);
  newSP = ((((uint16_t)high) & 0x00ff) << 4) | ((((uint16_t)low) & 0x00f0) >> 4);

  uint16_t temp = newST;
  newST = newSP;
  newSP = temp;

  writeOneByte(0x0a, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0b, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)));
  writeOneByte(0x0c, (uint8_t)((newSP & 0x0ff0) >> 4));
}

void invertVS() {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0d, 1, &low);
  readFromRegister(0x0e, 1, &high);
  newST = ((((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0e, 1, &low);
  readFromRegister(0x0f, 1, &high);
  newSP = ((((uint16_t)high) & 0x00ff) << 4) | ((((uint16_t)low) & 0x00f0) >> 4);

  uint16_t temp = newST;
  newST = newSP;
  newSP = temp;

  writeOneByte(0x0d, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0e, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)));
  writeOneByte(0x0f, (uint8_t)((newSP & 0x0ff0) >> 4));
}

void scaleVertical(uint16_t amountToAdd, bool subtracting) {
  uint16_t vscale = GBS::VDS_VSCALE::read();

  if (subtracting && (vscale - amountToAdd > 0)) {
    vscale -= amountToAdd;
  }
  else if (vscale + amountToAdd <= 1023) {
    vscale += amountToAdd;
  }

  SerialM.print("Scale Vert: "); SerialM.println(vscale);
  GBS::VDS_VSCALE::write(vscale);
}

void shiftVertical(uint16_t amountToAdd, bool subtracting) {
  typedef GBS::Tie<GBS::VDS_VB_ST, GBS::VDS_VB_SP> Regs;
  uint16_t vrst = GBS::VDS_VSYNC_RST::read() - FrameSync::getSyncLastCorrection();
  uint16_t vbst = 0, vbsp = 0;
  int16_t newVbst = 0, newVbsp = 0;

  Regs::read(vbst, vbsp);
  newVbst = vbst; newVbsp = vbsp;

  if (subtracting) {
    newVbst -= amountToAdd;
    newVbsp -= amountToAdd;
  }
  else {
    newVbst += amountToAdd;
    newVbsp += amountToAdd;
  }

  // handle the case where hbst or hbsp have been decremented below 0
  if (newVbst < 0) {
    newVbst = vrst + newVbst;
  }
  if (newVbsp < 0) {
    newVbsp = vrst + newVbsp;
  }

  // handle the case where vbst or vbsp have been incremented above vrstValue
  if (newVbst > (int16_t)vrst) {
    newVbst = newVbst - vrst;
  }
  if (newVbsp > (int16_t)vrst) {
    newVbsp = newVbsp - vrst;
  }

  Regs::write(newVbst, newVbsp);
  //SerialM.print("VSST: "); SerialM.print(newVbst); SerialM.print(" VSSP: "); SerialM.println(newVbsp);
}

void shiftVerticalUp() {
  shiftVertical(1, true);
}

void shiftVerticalDown() {
  shiftVertical(1, false);
}

void shiftVerticalUpIF() {
  // -4 to allow variance in source line count
  uint8_t offset = rto->videoStandardInput == 2 ? 4 : 1;
  uint16_t sourceLines = GBS::VPERIOD_IF::read() - offset;
  int16_t stop = GBS::IF_VB_SP::read();
  int16_t start = GBS::IF_VB_ST::read();

  if (stop < sourceLines && start < sourceLines) { stop += 1; start += 1; }
  else {
    start = 0; stop = 1;
  }
  GBS::IF_VB_SP::write(stop);
  GBS::IF_VB_ST::write(start);
}

void shiftVerticalDownIF() {
  uint8_t offset = rto->videoStandardInput == 2 ? 4 : 1;
  uint16_t sourceLines = GBS::VPERIOD_IF::read() - offset;
  int16_t stop = GBS::IF_VB_SP::read();
  int16_t start = GBS::IF_VB_ST::read();

  if (stop > 0 && start > 0) { stop -= 1; start -= 1; }
  else {
    start = sourceLines - 1; stop = sourceLines;
  }
  GBS::IF_VB_SP::write(stop);
  GBS::IF_VB_ST::write(start);
}

void setHSyncStartPosition(uint16_t value) {
  GBS::VDS_HS_ST::write(value);
}

void setHSyncStopPosition(uint16_t value) {
  GBS::VDS_HS_SP::write(value);
}

void setMemoryHblankStartPosition(uint16_t value) {
  GBS::VDS_HB_ST::write(value);
}

void setMemoryHblankStopPosition(uint16_t value) {
  GBS::VDS_HB_SP::write(value);
}

void setDisplayHblankStartPosition(uint16_t value) {
  GBS::VDS_DIS_HB_ST::write(value);
}

void setDisplayHblankStopPosition(uint16_t value) {
  GBS::VDS_DIS_HB_SP::write(value);
}

void setVSyncStartPosition(uint16_t value) {
  GBS::VDS_VS_ST::write(value);
}

void setVSyncStopPosition(uint16_t value) {
  GBS::VDS_VS_SP::write(value);
}

void setMemoryVblankStartPosition(uint16_t value) {
  GBS::VDS_VB_ST::write(value);
}

void setMemoryVblankStopPosition(uint16_t value) {
  GBS::VDS_VB_SP::write(value);
}

void setDisplayVblankStartPosition(uint16_t value) {
  GBS::VDS_DIS_VB_ST::write(value);
}

void setDisplayVblankStopPosition(uint16_t value) {
  GBS::VDS_DIS_VB_SP::write(value);
}

#if defined(ESP8266) // Arduino space saving
void getVideoTimings() {
  SerialM.println("");
  // get HRST
  SerialM.print("htotal: "); SerialM.println(GBS::VDS_HSYNC_RST::read());
  // get HS_ST
  SerialM.print("HS ST/SP     : "); SerialM.print(GBS::VDS_HS_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_HS_SP::read());
  // get HBST
  SerialM.print("HB ST/SP(dis): "); SerialM.print(GBS::VDS_DIS_HB_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_DIS_HB_SP::read());
  // get HBST(memory)
  SerialM.print("HB ST/SP     : "); SerialM.print(GBS::VDS_HB_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_HB_SP::read());
  SerialM.println("----");
  // get VRST
  SerialM.print("vtotal: "); SerialM.println(GBS::VDS_VSYNC_RST::read());
  // get V Sync Start
  SerialM.print("VS ST/SP     : "); SerialM.print(GBS::VDS_VS_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_VS_SP::read());
  // get VBST
  SerialM.print("VB ST/SP(dis): "); SerialM.print(GBS::VDS_DIS_VB_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_DIS_VB_SP::read());
  // get VBST (memory)
  SerialM.print("VB ST/SP     : "); SerialM.print(GBS::VDS_VB_ST::read());
  SerialM.print(" "); SerialM.println(GBS::VDS_VB_SP::read());
  // also IF offsets
  SerialM.print("IF_VB_ST/SP  : "); SerialM.print(GBS::IF_VB_ST::read());
  SerialM.print(" "); SerialM.println(GBS::IF_VB_SP::read());
}
#endif

void set_htotal(uint16_t htotal) {
  // ModeLine "1280x960" 108.00 1280 1376 1488 1800 960 961 964 1000 +HSync +VSync
  // front porch: H2 - H1: 1376 - 1280
  // back porch : H4 - H3: 1800 - 1488
  // sync pulse : H3 - H2: 1488 - 1376

  uint16_t h_blank_display_start_position = htotal - 1;
  uint16_t h_blank_display_stop_position = htotal - ((htotal * 3) / 4);
  uint16_t center_blank = ((h_blank_display_stop_position / 2) * 3) / 4; // a bit to the left
  uint16_t h_sync_start_position = center_blank - (center_blank / 2);
  uint16_t h_sync_stop_position = center_blank + (center_blank / 2);
  uint16_t h_blank_memory_start_position = h_blank_display_start_position - 1;
  uint16_t h_blank_memory_stop_position = h_blank_display_stop_position - (h_blank_display_stop_position / 50);

  GBS::VDS_HSYNC_RST::write(htotal);
  GBS::VDS_HS_ST::write(h_sync_start_position);
  GBS::VDS_HS_SP::write(h_sync_stop_position);
  GBS::VDS_DIS_HB_ST::write(h_blank_display_start_position);
  GBS::VDS_DIS_HB_SP::write(h_blank_display_stop_position);
  GBS::VDS_HB_ST::write(h_blank_memory_start_position);
  GBS::VDS_HB_SP::write(h_blank_memory_stop_position);
}

void set_vtotal(uint16_t vtotal) {
  // VS stop - VB start must stay constant to avoid vertical wiggle
  // VS stop - VS start must stay constant to maintain sync
  uint16_t VDS_DIS_VB_ST = (((uint32_t)vtotal * 24) / 25) - 4; // just below vtotal
  uint16_t VDS_DIS_VB_SP = 8; // positive, just above 0
  // Offset by maxCorrection to prevent front porch from going negative
  uint16_t v_sync_start_position = ((uint32_t)vtotal * 961) / 1000;
  uint16_t v_sync_stop_position = ((uint32_t)vtotal * 241) / 250;
  // most low line count formats have negative sync!
  // exception: 1024x768 (1344x806 total) has both sync neg.
  // also 1360x768 (1792x795 total)
  if ((vtotal < 530) || (vtotal >=803 && vtotal <= 809) || (vtotal >=793 && vtotal <= 798)) {
    uint16_t temp = v_sync_start_position;
    v_sync_start_position = v_sync_stop_position;
    v_sync_stop_position = temp;
  }

  //uint16_t VDS_VB_ST = VDS_DIS_VB_SP - 4;
  //uint16_t VDS_VB_SP = VDS_DIS_VB_SP - 2;

  GBS::VDS_VSYNC_RST::write(vtotal);
  GBS::VDS_VS_ST::write(v_sync_start_position);
  GBS::VDS_VS_SP::write(v_sync_stop_position);
  GBS::VDS_VB_ST::write(0);
  GBS::VDS_VB_SP::write(2);
  GBS::VDS_DIS_VB_ST::write(VDS_DIS_VB_ST);
  GBS::VDS_DIS_VB_SP::write(VDS_DIS_VB_SP);

  // also reset IF offset here
  GBS::IF_VB_ST::write(21);
  GBS::IF_VB_SP::write(22);
}

void enableDebugPort() {
  GBS::PAD_BOUT_EN::write(1); // output to pad enabled
  GBS::IF_TEST_EN::write(1);
  GBS::IF_TEST_SEL::write(3); // IF vertical period signal
  GBS::TEST_BUS_SEL::write(0xa); // test bus to SP
  GBS::TEST_BUS_EN::write(1);
  GBS::TEST_BUS_SP_SEL::write(0x0f); // SP test signal select (vsync in, after SOG separation)
  // SP test bus enable bit is included in TEST_BUS_SP_SEL
  GBS::VDS_TEST_EN::write(1); // VDS test enable
}

void applyBestHTotal(uint16_t bestHTotal) {
  boolean isCustomPreset = GBS::ADC_0X00_RESERVED_5::read();
  if (isCustomPreset) {
    SerialM.println("custom preset: no htotal change");
    return;
  }
  uint16_t orig_htotal = GBS::VDS_HSYNC_RST::read();
  int diffHTotal = bestHTotal - orig_htotal;
  uint16_t diffHTotalUnsigned = abs(diffHTotal);
  boolean isLargeDiff = (diffHTotalUnsigned * 10) > orig_htotal ? true : false;
  boolean requiresScalingCorrection = GBS::VDS_HSCALE::read() < 512; // output distorts if less than 512 but can be corrected

  // rto->forceRetime = true means the correction should be forced (command '.')
  // may want to check against multi clock snes
  if ((!rto->outModePassThroughWithIf || rto->forceRetime == true) && bestHTotal > 400) {
    rto->forceRetime = false;
    
    // move blanking (display)
    uint16_t h_blank_display_start_position = GBS::VDS_DIS_HB_ST::read() + diffHTotal;
    uint16_t h_blank_display_stop_position = GBS::VDS_DIS_HB_SP::read() + diffHTotal;

    // move HSync
    uint16_t h_sync_start_position = GBS::VDS_HS_ST::read();
    uint16_t h_sync_stop_position = GBS::VDS_HS_SP::read();
    h_sync_start_position += diffHTotal;
    h_sync_stop_position += diffHTotal;

    uint16_t h_blank_memory_start_position = GBS::VDS_HB_ST::read();
    uint16_t h_blank_memory_stop_position = GBS::VDS_HB_SP::read();
    h_blank_memory_start_position += diffHTotal;
    h_blank_memory_stop_position  += diffHTotal;

    if (isLargeDiff) {
      SerialM.println("large diff!");
    }

    if (requiresScalingCorrection) {
      h_blank_memory_start_position &= 0xfffe;
    }

    // try to fix over / underflows (okay if bestHtotal > base, only partially okay if otherwise)
    if (h_sync_start_position > bestHTotal) {
      h_sync_start_position = 4095 - h_sync_start_position;
    }
    if (h_sync_stop_position > bestHTotal) {
      h_sync_stop_position = 4095 - h_sync_stop_position;
    }
    if (h_blank_display_start_position > bestHTotal) {
      h_blank_display_start_position = 4095 - h_blank_display_start_position;
    }
    if (h_blank_display_stop_position > bestHTotal) {
      h_blank_display_stop_position = 4095 - h_blank_display_stop_position;
    }
    if (h_blank_memory_start_position > bestHTotal) {
      h_blank_memory_start_position = 4095 - h_blank_memory_start_position;
    }
    if (h_blank_memory_stop_position > bestHTotal) {
      h_blank_memory_stop_position = 4095 - h_blank_memory_stop_position;
    }

    GBS::VDS_HSYNC_RST::write(bestHTotal);
    GBS::VDS_HS_ST::write(h_sync_start_position);
    GBS::VDS_HS_SP::write(h_sync_stop_position);
    GBS::VDS_DIS_HB_ST::write(h_blank_display_start_position);
    GBS::VDS_DIS_HB_SP::write(h_blank_display_stop_position);
    GBS::VDS_HB_ST::write(h_blank_memory_start_position);
    GBS::VDS_HB_SP::write(h_blank_memory_stop_position);
    // IF htotal to pll divider (number of sampled pixels)
    //GBS::IF_HSYNC_RST::write((GBS::PLLAD_MD::read() / 2) - 2);
    //GBS::IF_LINE_SP::write((GBS::PLLAD_MD::read() / 2) - 1);
    // IF 1_1a to minimum
    //GBS::IF_HB_SP2::write(0x08);
  }
  SerialM.print("Base: "); SerialM.print(orig_htotal);
  SerialM.print(" Best: "); SerialM.println(bestHTotal);
  // todo: websocket breaks on this if diffHTotal is negative
  //SerialM.print(" Diff: "); SerialM.println(diffHTotal);
}

void doPostPresetLoadSteps() {
  boolean isCustomPreset = GBS::ADC_0X00_RESERVED_5::read();
  GBS::SP_DIS_SUB_COAST::write(1); // disable initially, gets activated in updatecoastposition
  GBS::SP_HCST_AUTO_EN::write(0); // needs to be off (making sure)
  
  if (!isCustomPreset) {
    setAdcParametersGainAndOffset(); // 0x3f + 0x7f
  }
  GBS::ADC_TEST::write(0); // in case it was set
  
  // 0 segment
  GBS::GPIO_CONTROL_00::write(0xff); // all GPIO pins regular GPIO
  GBS::GPIO_CONTROL_01::write(0x00); // all GPIO outputs disabled
  rto->clampPositionIsSet = false;
  rto->coastPositionIsSet = false;
  rto->continousStableCounter = 0;
  GBS::SP_NO_CLAMP_REG::write(1); // (keep) clamp disabled, to be enabled when position determined
  GBS::DAC_RGBS_PWDNZ::write(0); // disable DAC here, enable later (should already be off though)
  
  // test: set IF_HSYNC_RST based on pll divider 5_12/13
  //GBS::IF_LINE_SP::write((GBS::PLLAD_MD::read() / 2) - 1);
  //GBS::IF_HSYNC_RST::write((GBS::PLLAD_MD::read() / 2) - 2); // remember: 1_0e always -1
  
  // IF initial position is 1_0e/0f IF_HSYNC_RST exactly. But IF_INI_ST needs to be a few pixels before that.
  // IF_INI_ST - 1 causes an interresting effect when the source switches to interlace.
  // IF_INI_ST - 2 is the first safe setting // exception: edtv+ presets: need to be more exact
  if (rto->videoStandardInput <= 2) {
    GBS::IF_INI_ST::write(GBS::IF_HSYNC_RST::read() - 4);
    if (rto->videoStandardInput == 2) {  // exception for PAL (with i.e. PSX) default preset
      GBS::IF_INI_ST::write(GBS::IF_INI_ST::read() - 64);
    }
  }
  else {
    GBS::IF_INI_ST::write(GBS::IF_HSYNC_RST::read() - 1); // -0 also seems to work
  }

  if (!isCustomPreset) {
    if (rto->videoStandardInput == 3) { // ED YUV 60
      // p-scan ntsc, need to either double adc data rate and halve vds scaling
      // or disable line doubler (better)
      GBS::PLLAD_KS::write(1); // 5_16
      GBS::VDS_VSCALE::write(512); // 548
      GBS::VDS_HSCALE::write(768);
      GBS::VDS_V_DELAY::write(0); // filter 3_24 2 off
      GBS::VDS_TAP6_BYPS::write(1); // 3_24 3 disable filter (jailbars)
      GBS::IF_HS_TAP11_BYPS::write(1); // 1_02 4 disable filter
      GBS::IF_PRGRSV_CNTRL::write(1); // 1_00 6
      GBS::IF_HS_DEC_FACTOR::write(0); // 1_0b 4+5
      GBS::IF_LD_SEL_PROV::write(1); // 1_0b 7
      GBS::IF_LD_RAM_BYPS::write(1); // no LD 1_0c 0
      // horizontal shift
      GBS::IF_HB_SP2::write(0xb0); // a bit too much to the left
      GBS::IF_HB_ST2::write(0x20); // 1_18 necessary
      //GBS::IF_HBIN_ST::write(1104); // 1_24 // no effect seen but may be necessary
      GBS::IF_HBIN_SP::write(0x78); // 1_26
      // vertical shift
      GBS::IF_VB_ST::write(6); // 514
      GBS::IF_VB_SP::write(7); // 514
      // display lower blanking
      setDisplayVblankStopPosition(40);
      setDisplayVblankStartPosition(982);
      setMemoryVblankStartPosition(2);
      setMemoryVblankStopPosition(4);
      // display hor. blanking
      GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - 44);
      GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() - 34);
    }
    else if (rto->videoStandardInput == 4) { // ED YUV 50
      // p-scan pal, need to either double adc data rate and halve vds scaling
      // or disable line doubler (better)
      GBS::PLLAD_KS::write(1); // 5_16
      GBS::VDS_VSCALE::write(683);
      GBS::VDS_HSCALE::write(576);
      GBS::VDS_V_DELAY::write(1); // filter 3_24 2 on
      GBS::VDS_TAP6_BYPS::write(1); // 3_24 3 disable filter (jailbars)
      GBS::MADPT_Y_DELAY::write(1); // some shift
      GBS::IF_INI_ST::write(0x4d0);
      GBS::IF_HS_TAP11_BYPS::write(0); // 1_02 4 enable filter
      GBS::IF_PRGRSV_CNTRL::write(1); // 1_00 6
      GBS::IF_HS_DEC_FACTOR::write(0); // 1_0b 4+5
      GBS::IF_LD_SEL_PROV::write(1); // 1_0b 7
      GBS::IF_LD_RAM_BYPS::write(1); // no LD 1_0c 0
      // vertical shift
      GBS::IF_VB_ST::write(610);
      GBS::IF_VB_SP::write(611);
      // horizontal shift
      GBS::IF_HB_SP2::write(0xc0);
      GBS::IF_HB_ST2::write(0x20); // check!
      GBS::IF_HBIN_SP::write(0x90); // 1_26
      GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() + 64);
      GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() + 64);
      GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - 46);
      GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() + 2);
      // display blanking
      setDisplayVblankStopPosition(42);
      setDisplayVblankStartPosition(934);
      setMemoryVblankStartPosition(10);
      setMemoryVblankStopPosition(12);
    }
    else if (rto->videoStandardInput == 5) { // 720p
      GBS::SP_HD_MODE::write(1); // tri level sync
      GBS::ADC_CLK_ICLK2X::write(0);
      GBS::PLLAD_KS::write(0); // 5_16
      GBS::VDS_VSCALE::write(768); // hardcoded for now
      GBS::VDS_HSCALE::write(683); // hardcoded for now
      GBS::IF_PRGRSV_CNTRL::write(1); // progressive
      //GBS::IF_SEL_WEN::write(1); // and HD (not interlaced)
      GBS::IF_HS_DEC_FACTOR::write(0);
    }
    else if (rto->videoStandardInput == 6 || rto->videoStandardInput == 7) { // 1080i/p
      GBS::SP_HD_MODE::write(1); // tri level sync
      GBS::ADC_CLK_ICLK2X::write(0);
      GBS::PLLAD_KS::write(0); // 5_16
      GBS::IF_PRGRSV_CNTRL::write(1);
      GBS::IF_HS_DEC_FACTOR::write(0);
    }
  }
  rto->outModePassThroughWithIf = 0; // could be 1 if it was active, but overriden by preset load
  setSpParameters();
  setAndUpdateSogLevel(rto->currentLevelSOG);

  // ADC
  //GBS::ADC_TR_RSEL::write(2); // ADC_TR_RSEL = 2 test
  //GBS::ADC_TR_ISEL::write(0); // leave current at default
  // high color gain so auto adjust can work on it
  if (uopt->enableAutoGain == 1 && !rto->inputIsYpBpR) {
    GBS::ADC_RGCTRL::write(0x40);
    GBS::ADC_GGCTRL::write(0x40);
    GBS::ADC_BGCTRL::write(0x40);
    /*GBS::ADC_ROFCTRL::write(0x43);
    GBS::ADC_GOFCTRL::write(0x43);
    GBS::ADC_BOFCTRL::write(0x43);*/
    GBS::DEC_TEST_ENABLE::write(1);
  }
  else {
    GBS::DEC_TEST_ENABLE::write(0);
  }

  if (!isCustomPreset) {
    if (rto->inputIsYpBpR == true) {
      applyYuvPatches();
    }
    else {
      applyRGBPatches();
    }
  }
  GBS::PLLAD_R::write(2);
  GBS::PLLAD_S::write(2);

  GBS::PLLAD_PDZ::write(1); // in case it was off
  //update rto phase variables
  rto->phaseADC = GBS::PA_ADC_S::read();
  rto->phaseSP = 15; // can hardcode this to 15 now
  GBS::DEC_WEN_MODE::write(1); // keeps ADC phase much more consistent. around 4 lock positions vs totally random
  GBS::DEC_IDREG_EN::write(1);
  // jitter sync off for all modes
  GBS::SP_JITTER_SYNC::write(0);

  if (!isCustomPreset) {
    // memory timings, anti noise
    GBS::PB_CUT_REFRESH::write(1); // test, helps with PLL=ICLK mode artefacting
    GBS::PB_REQ_SEL::write(3); // PlayBack 11 High request Low request
    GBS::PB_GENERAL_FLAG_REG::write(0x3f); // 4_2D max
    //GBS::PB_MAST_FLAG_REG::write(0x16); // 4_2c should be set by preset
    GBS::MEM_INTER_DLYCELL_SEL::write(1); // 4_12 to 0x05
    GBS::MEM_CLK_DLYCELL_SEL::write(0); // 4_12 to 0x05
    GBS::MEM_FBK_CLK_DLYCELL_SEL::write(1); // 4_12 to 0x05
  }
  resetPLLAD(); // turns on pllad
  delay(20);
  resetDigital();

  rto->autoBestHtotalEnabled = true; // will re-detect whether debug wire is present
  Menu::init();
  enableDebugPort();

  GBS::PAD_SYNC_OUT_ENZ::write(1); // delay sync output
  //enableVDS();
  FrameSync::reset();
  rto->syncLockFailIgnore = 2;
  //ResetSDRAM(); // already done in resetPLL
  GBS::DAC_RGBS_PWDNZ::write(1); // enable DAC
  unsigned long timeout = millis();
  while (getVideoMode() == 0 && millis() - timeout < 800) { delay(1); } // wifi stack // stability
  //SerialM.print("to1 is: "); SerialM.println(millis() - timeout);

  setPhaseSP(); setPhaseADC();
  for (uint8_t i = 0; i < 8; i++) { // somehow this increases phase position reliability
    advancePhase();
  }
  GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
  GBS::INTERRUPT_CONTROL_00::write(0x00);
  GBS::PAD_SYNC_OUT_ENZ::write(0); // output sync > display goes on
  SerialM.println("post preset done");
  rto->applyPresetDoneStage = 1;
  rto->applyPresetDoneTime = millis();
}

void applyPresets(uint8_t result) {
  if (result == 1) {
    SerialM.println("60Hz ");
    if (uopt->presetPreference == 0) {
      writeProgramArrayNew(ntsc_240p);
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    else if (uopt->presetPreference == 3) {
      writeProgramArrayNew(ntsc_1280x720);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      writeProgramArrayNew(ntsc_1280x1024);
    }
#endif
  }
  else if (result == 2) {
    SerialM.println("50Hz ");
    if (uopt->presetPreference == 0) {
      writeProgramArrayNew(pal_240p);
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(pal_feedbackclock);
    }
    else if (uopt->presetPreference == 3) {
      writeProgramArrayNew(pal_1280x720);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      writeProgramArrayNew(pal_1280x1024);
    }
#endif
  }
  else if (result == 3) {
    SerialM.println("60Hz EDTV ");
    // ntsc base
    if (uopt->presetPreference == 0) {
      writeProgramArrayNew(ntsc_240p);
    }
    else if (uopt->presetPreference == 1) {
      //writeProgramArrayNew(ntsc_feedbackclock); // not supported atm
      writeProgramArrayNew(ntsc_240p);
    }
    else if (uopt->presetPreference == 3) {
      //writeProgramArrayNew(ntsc_1280x720); // not supported atm
      writeProgramArrayNew(ntsc_240p);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      //writeProgramArrayNew(ntsc_1280x1024); // not supported atm
      writeProgramArrayNew(ntsc_240p);
    }
#endif
  }
  else if (result == 4) {
    SerialM.println("50Hz EDTV ");
    // pal base
    if (uopt->presetPreference == 0) {
      writeProgramArrayNew(pal_240p);
    }
    else if (uopt->presetPreference == 1) {
      //writeProgramArrayNew(pal_feedbackclock); // not supported atm
      writeProgramArrayNew(pal_240p);
    }
    else if (uopt->presetPreference == 3) {
      //writeProgramArrayNew(pal_1280x720); // not supported atm
      writeProgramArrayNew(pal_240p);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      writeProgramArrayNew(pal_240p);
    }
#endif
  }
  else if (result == 5) {
    SerialM.println("720p 60Hz HDTV ");
    // use bypass mode for all configs
    rto->outModePassThroughWithIf = 0;
    rto->videoStandardInput = 5;
    passThroughWithIfModeSwitch();
    return;

    /*if (uopt->presetPreference == 0) {
      writeProgramArrayNew(ntsc_240p);
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    else if (uopt->presetPreference == 3) {
      writeProgramArrayNew(ntsc_1280x720);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      writeProgramArrayNew(ntsc_1280x1024);
    }
#endif*/
  }
  else if (result == 6 || result == 7) {
    if (result == 6) {
      SerialM.println("1080i 60Hz HDTV ");
      rto->videoStandardInput = 6;
    }
    if (result == 7) {
      SerialM.println("1080p 60Hz HDTV ");
      rto->videoStandardInput = 7;
    }
    // use bypass mode for all configs
    rto->outModePassThroughWithIf = 0;
    passThroughWithIfModeSwitch();
    return;

    /*if (uopt->presetPreference == 0) {
      writeProgramArrayNew(ntsc_240p);
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    else if (uopt->presetPreference == 3) {
      writeProgramArrayNew(ntsc_1280x720);
    }
#if defined(ESP8266)
    else if (uopt->presetPreference == 2) {
      SerialM.println("(custom)");
      const uint8_t* preset = loadPresetFromSPIFFS(result);
      writeProgramArrayNew(preset);
    }
    else if (uopt->presetPreference == 4) {
      writeProgramArrayNew(ntsc_1280x1024);
    }
#endif*/
  }
  else if (result == 15) {
    SerialM.println("RGBHV bypass ");
    writeProgramArrayNew(ntsc_240p);
    bypassModeSwitch_RGBHV();
  }
  else {
    SerialM.println("Unknown timing! ");
    rto->videoStandardInput = 0; // mark as "no sync" for syncwatcher
    inputAndSyncDetect();
    //resetModeDetect();
    delay(300);
    return;
  }

  rto->videoStandardInput = result;
  doPostPresetLoadSteps();
}

void enableDeinterlacer() {
  if (rto->videoStandardInput != 15 && !rto->outModePassThroughWithIf) {
    GBS::SFTRST_DEINT_RSTZ::write(1);
  }
  rto->deinterlacerWasTurnedOff = false;
}

// acts as a coasting mechanism on sync disturbance
void disableDeinterlacer() {
  if (rto->deinterlacerWasTurnedOff == false) {
    GBS::SFTRST_DEINT_RSTZ::write(0);
  }
  rto->deinterlacerWasTurnedOff = true;
}

static uint8_t getVideoMode() {
  uint8_t detectedMode = 0;

  if (rto->videoStandardInput == 15) { // check RGBHV first
    detectedMode = GBS::STATUS_16::read();
    if ((detectedMode & 0x0a) > 0) { // bit 1 or 3 active?
      return 15; // still RGBHV bypass
    }
  }

  detectedMode = GBS::STATUS_00::read();
  // note: if stat0 == 0x07, it's supposedly stable. if we then can't find a mode, it must be an MD problem
  if ((detectedMode & 0x80) == 0x80) { // bit 7: SD flag (480i, 480P, 576i, 576P)
    if ((detectedMode & 0x08) == 0x08) return 1; // ntsc interlace
    if ((detectedMode & 0x20) == 0x20) return 2; // pal interlace
    if ((detectedMode & 0x10) == 0x10) return 3; // edtv 60 progressive
    if ((detectedMode & 0x40) == 0x40) return 4; // edtv 50 progressive
  }

  //detectedMode = GBS::STATUS_05::read(); // doesn't work
  //if ((detectedMode & 0x01) == 0x01) return 1; // custom mode (ntsc-i also)

  detectedMode = GBS::STATUS_03::read();
  if ((detectedMode & 0x10) == 0x10) { return 5; } // hdtv 720p
  detectedMode = GBS::STATUS_04::read();
  if ((detectedMode & 0x20) == 0x20) { // hd mode on
    if ((detectedMode & 0x61) == 0x61) {
      // hdtv 1080i // 576p mode tends to get misdetected as this, even with all the checks
      // real 1080i (PS2): h:199 v:1124
      // misdetected 576p (PS2): h:215 v:1249
      if (GBS::VPERIOD_IF::read() < 1160) {
        return 6;
      }
    }
    if ((detectedMode & 0x10) == 0x10) {
      return 7; // hdtv 1080p
    }
  }

  return 0; // unknown mode
}

// if testbus has 0x05, sync is present and line counting active. if it has 0x04, sync is present but no line counting
boolean getSyncPresent() {
  uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
  uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(0xa);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(0x0f);
  }
  uint16_t readout = GBS::TEST_BUS::read();
  if (((readout & 0x0500) == 0x0500) || ((readout & 0x0500) == 0x0400)) {
    if (debug_backup != 0xa) {
      GBS::TEST_BUS_SEL::write(debug_backup);
    }
    if (debug_backup_SP != 0x0f) {
      GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
    }
    return true;
  }
  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(debug_backup);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
  }
  return false;
}

boolean getSyncStable() {
  if (rto->videoStandardInput == 15) { // check RGBHV first
    if (GBS::STATUS_MISC_PLLAD_LOCK::read() == 1) {
      return true;
    }
    else {
      return false;
    }
  }

  uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
  uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(0xa);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(0x0f);
  }
  //todo: intermittant sync loss can read as okay briefly
  //if ((GBS::TEST_BUS::read() & 0x0500) == 0x0500) {
  if ((GBS::TEST_BUS::read() & 0x0fff) > 0x0480) {
    if (debug_backup != 0xa) {
      GBS::TEST_BUS_SEL::write(debug_backup);
    }
    if (debug_backup_SP != 0x0f) {
      GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
    }
    return true;
  }
  if (debug_backup != 0xa) {
    GBS::TEST_BUS_SEL::write(debug_backup);
  }
  if (debug_backup_SP != 0x0f) {
    GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
  }
  return false;
}

void togglePhaseAdjustUnits() {
  GBS::PA_SP_BYPSZ::write(0); // yes, 0 means bypass on here
  GBS::PA_SP_BYPSZ::write(1);
}

void advancePhase() {
  rto->phaseADC = (rto->phaseADC + 1) & 0x1f;
  setPhaseADC();
}

void movePhaseThroughRange() {
  for (uint8_t i = 0; i < 128; i++) { // 4x for 4x oversampling?
    advancePhase();
  }
}

void setPhaseSP() {
  GBS::PA_SP_LAT::write(0); // latch off
  GBS::PA_SP_S::write(rto->phaseSP);
  GBS::PA_SP_LAT::write(1); // latch on
}

void setPhaseADC() {
  GBS::PA_ADC_LAT::write(0);
  GBS::PA_ADC_S::write(rto->phaseADC);
  GBS::PA_ADC_LAT::write(1);
}

void updateCoastPosition() {
  if (rto->coastPositionIsSet) {
    return;
  }

  if (rto->videoStandardInput <= 2) { // including 0 (reset condition)
    GBS::SP_PRE_COAST::write(6); // SP test: 9
    GBS::SP_POST_COAST::write(16); // SP test: 9
  }
  else if (rto->videoStandardInput <= 6) {
    GBS::SP_PRE_COAST::write(0x06);
    GBS::SP_POST_COAST::write(0x06);
  }
  else if (rto->videoStandardInput == 7) { // 1080p
    GBS::SP_PRE_COAST::write(0x06);
    GBS::SP_POST_COAST::write(0x16); // quite a lot
  }
  else if (rto->videoStandardInput == 15) {
    GBS::SP_PRE_COAST::write(0x00);
    GBS::SP_POST_COAST::write(0x00);
    return;
  }

  int16_t inHlength = 0;
  for (uint8_t i = 0; i < 8; i++) {
      inHlength += ((GBS::HPERIOD_IF::read() + 1) & 0xfffe); // psx jitters between 427, 428
  }
  inHlength = inHlength >> 1; // /8 , *4

  if (inHlength > 0) {
      GBS::SP_H_CST_ST::write(inHlength >> 4); // low but not near 0 (typical: 0x6a)
      GBS::SP_H_CST_SP::write(inHlength - 32); //snes minimum: inHlength -12 (only required in 239 mode)
      /*SerialM.print("coast ST: "); SerialM.print("0x"); SerialM.print(inHlength >> 4, HEX); 
      SerialM.print("  "); 
      SerialM.print("SP: "); SerialM.print("0x"); SerialM.println(inHlength - 32, HEX);*/
      GBS::SP_H_PROTECT::write(0);
      GBS::SP_DIS_SUB_COAST::write(0); // enable hsync coast
      GBS::SP_HCST_AUTO_EN::write(0); // needs to be off (making sure)
      rto->coastPositionIsSet = true;
  }
}

void updateClampPosition() {
  if (rto->clampPositionIsSet) {
    return;
  }
  if (rto->videoStandardInput == 0) {
    // don't do anything, clamp is disabled
    return;
  }

  uint16_t inHlength = GBS::STATUS_SYNC_PROC_HTOTAL::read();
  if (inHlength < 100 || inHlength > 4095) { 
      SerialM.println("updateClampPosition: not stable yet"); // assert: must be valid
      return;
  }

  GBS::SP_CLP_SRC_SEL::write(0); // 0: 27Mhz clock 1: pixel clock
  GBS::SP_CLAMP_INV_REG::write(0); // clamp normal (no invert)
  uint16_t start = inHlength * 0.009f;
  uint16_t stop = inHlength * 0.022f;
  if (rto->videoStandardInput == 15) {
    //RGBHV bypass
    start = inHlength * 0.034f;
    stop = inHlength * 0.06f;
    GBS::SP_CLAMP_INV_REG::write(0); // clamp normal
  }
  else if (rto->inputIsYpBpR) {
    // YUV
    // sources via composite > rca have a colorburst, but we don't care and optimize for on-spec
    if (rto->videoStandardInput <= 2) {
      start = inHlength * 0.0136f;
      stop = inHlength * 0.041f;
    }
    else if (rto->videoStandardInput == 3) {
      start = inHlength * 0.008f;
      stop = inHlength * 0.0168f;
    }
    else if (rto->videoStandardInput == 4) {
      start = inHlength * 0.010f; // 0.014f
      stop = inHlength * 0.027f;
    }
    else if (rto->videoStandardInput == 5) { // HD / tri level sync
      start = inHlength * 0.0232f;
      stop = inHlength * 0.04f; // 720p
    }
    else if (rto->videoStandardInput == 6) {
      start = inHlength * 0.012f;
      stop = inHlength * 0.0305f; // 1080i
    }
    else if (rto->videoStandardInput == 7) {
      start = inHlength * 0.0015f;
      stop = inHlength * 0.01f; // 1080p
    }
  }
  else if (!rto->inputIsYpBpR) {
    // regular RGBS
    start = inHlength * 0.009f;
    stop = inHlength * 0.022f;
  }

  GBS::SP_CS_CLP_ST::write(start);
  GBS::SP_CS_CLP_SP::write(stop);

  /*SerialM.print("clamp ST: "); SerialM.print("0x"); SerialM.print(start, HEX); 
  SerialM.print("  ");
  SerialM.print("SP: "); SerialM.print("0x"); SerialM.println(stop, HEX);*/

  GBS::SP_NO_CLAMP_REG::write(0);
  rto->clampPositionIsSet = true;
}

// use t5t00t2 and adjust t5t11t5 to find this sources ideal sampling clock for this preset (affected by htotal)
// 2431 for psx, 2437 for MD
// in this mode, sampling clock is free to choose
void passThroughWithIfModeSwitch() {
  SerialM.print("pass-through ");
  if (rto->outModePassThroughWithIf == 0) { // then enable pass-through mode
    SerialM.println("on");
    // first load default presets
    if (rto->videoStandardInput == 2 || rto->videoStandardInput == 4) {
      writeProgramArrayNew(pal_240p);
      doPostPresetLoadSteps();
    }
    else {
      writeProgramArrayNew(ntsc_240p);
      doPostPresetLoadSteps();
    }
    rto->autoBestHtotalEnabled = false; // disable while in this mode (need to set this after initial preset loading)
    GBS::PAD_SYNC_OUT_ENZ::write(1); // no sync out yet
    GBS::RESET_CONTROL_0x46::write(0); // 0_46 all off first, VDS + IF enabled later
    // from RGBHV tests: the memory bus can be tri stated for noise reduction
    GBS::PAD_TRI_ENZ::write(1); // enable tri state
    GBS::PLL_MS::write(2); // select feedback clock (but need to enable tri state!)
    GBS::MEM_PAD_CLK_INVERT::write(0); // helps also
    GBS::OUT_SYNC_SEL::write(2);
    if (!rto->inputIsYpBpR) { // RGB input (is fine atm)
      GBS::DAC_RGBS_ADC2DAC::write(1); // bypass IF + VDS for RGB sources (YUV needs the VDS for YUV > RGB)
      GBS::SP_HS_LOOP_SEL::write(0); // (5_57_6) // with = 0, 5_45 and 5_47 set output
      //GBS::SP_HS_PROC_INV_REG::write(0); // (5_56_5) do not invert HS (5_57_6 = 0)
      //GBS::SP_CS_HS_ST::write(0x710); // invert sync detection
    }
    else { // YUV input (do sync inversion?)
      GBS::DAC_RGBS_ADC2DAC::write(0); // use IF + VDS for YUV sources (VDS for for YUV > RGB)
      GBS::SP_HS_LOOP_SEL::write(0); // (5_57_6) // with = 0, 5_45 and 5_47 set output
      //GBS::SP_HS_PROC_INV_REG::write(0); // (5_56_5) do not invert HS (5_57_6 = 0)
      GBS::SFTRST_IF_RSTZ::write(1); // need IF
      GBS::SFTRST_VDS_RSTZ::write(1); // need VDS
    }
    GBS::VDS_HSCALE_BYPS::write(1);
    GBS::VDS_VSCALE_BYPS::write(1);
    GBS::VDS_SYNC_EN::write(1); // VDS sync to synclock
    GBS::VDS_HB_ST::write(0x00);
    GBS::VDS_HB_SP::write(0x10);
    GBS::VDS_DIS_HB_SP::write(220); // my crt
    GBS::VDS_DIS_HB_ST::write(0); // my crt 
    GBS::VDS_DIS_VB_SP::write(8); // my crt
    GBS::VDS_DIS_VB_ST::write(0); // my crt 
    // test!
    GBS::VDS_BLK_BF_EN::write(0); // blanking setup: 0 = off, 1 = on
    GBS::VDS_HSYNC_RST::write(0xfff); // max
    GBS::VDS_VSYNC_RST::write(0x7ff); // max
    GBS::PLLAD_MD::write(0x768); // psx 256, 320, 384 pix
    GBS::PLLAD_ICP::write(6);
    GBS::PLLAD_FS::write(0); // high gain needs to be off, else sync dropouts on LCD with SNES
    if (rto->videoStandardInput <= 2) {
      GBS::SP_CS_HS_ST::write(0);
      GBS::SP_CS_HS_SP::write(0x6c); // with PLLAD_MD = 0x768
    }
    else if (rto->videoStandardInput == 3) {
      GBS::SP_CS_HS_ST::write(0x50);
      GBS::SP_CS_HS_SP::write(0xb0); // with PLLAD_MD = 0x768
    }
    else if (rto->videoStandardInput == 4) {
      GBS::SP_CS_HS_ST::write(0x78);
      GBS::SP_CS_HS_SP::write(0xcc); // with PLLAD_MD = 0x768
    }
    else if (rto->videoStandardInput <= 7) {
      GBS::SP_CS_HS_ST::write(0x6c);
      GBS::SP_CS_HS_SP::write(0xc8); // with PLLAD_MD = 0x768
      GBS::PLLAD_FS::write(1); // 720p and up need high gain
      GBS::PLLAD_KS::write(1);
      if (rto->videoStandardInput == 7) {
        GBS::SP_CS_HS_ST::write(0x44); // overwrite
        GBS::SP_POST_COAST::write(0x16); // quite a lot ><
      }
      //GBS::SP_HS_PROC_INV_REG::write(1); // invert hs
      //GBS::SP_VS_PROC_INV_REG::write(1); // invert vs
    }
    latchPLLAD();
    delay(10);
    GBS::PB_BYPASS::write(1);
    GBS::PLL648_CONTROL_01::write(0x35);
    rto->phaseSP = 15; // eventhough i had this fancy bestphase function :p
    setPhaseSP(); // snes likes 12, was 4. if misplaced, creates single wiggly line
    rto->phaseADC = 0; setPhaseADC(); // was 18, exactly what causes issues every 4 attempts!
    GBS::SP_SDCS_VSST_REG_H::write(0x00); // S5_3B
    GBS::SP_SDCS_VSSP_REG_H::write(0x00); // S5_3B
    GBS::SP_SDCS_VSST_REG_L::write(0x02); // S5_3F
    GBS::SP_SDCS_VSSP_REG_L::write(0x0b); // S5_40 (test with interlaced sources)
    if (rto->videoStandardInput == 1 || rto->videoStandardInput == 6) {
      GBS::SP_SDCS_VSSP_REG_L::write(0x0c); // S5_40 (interlaced)
    }
    // IF
    GBS::IF_HS_TAP11_BYPS::write(1); // 1_02 bit 4 filter off looks better
    GBS::IF_HS_Y_PDELAY::write(3); // 1_02 bits 5+6
    GBS::IF_LD_RAM_BYPS::write(1);
    GBS::IF_HS_DEC_FACTOR::write(0);
    GBS::IF_HSYNC_RST::write(0x7ff); // (lineLength) // must be set for 240p at least
    GBS::IF_HBIN_SP::write(0x02); // must be even for 240p, adjusts left border at 0xf1+
    GBS::IF_HB_ST::write(0); // S1_10
    GBS::IF_HB_ST2::write(0); // S1_18 // just move the bar out of the way
    GBS::IF_HB_SP2::write(8); // S1_1a // just move the bar out of the way
    delay(30);
    GBS::SFTRST_SYNC_RSTZ::write(0); // reset SP 0_47 bit 2
    GBS::SFTRST_SYNC_RSTZ::write(1);
    delay(30);
    GBS::PAD_SYNC_OUT_ENZ::write(0); // sync out now
    rto->outModePassThroughWithIf = 1;
    delay(100);
  }
  else { // switch back
    SerialM.println("off");
    rto->autoBestHtotalEnabled = true; // enable again
    rto->outModePassThroughWithIf = 0;
    applyPresets(getVideoMode());
  }
}

void bypassModeSwitch_SOG() {
  static uint16_t oldPLLAD = 0;
  static uint8_t oldPLL648_CONTROL_01 = 0;
  static uint8_t oldPLLAD_ICP = 0;
  static uint8_t old_0_46 = 0;
  static uint8_t old_5_3f = 0;
  static uint8_t old_5_40 = 0;
  static uint8_t old_5_3e = 0;

  if (GBS::DAC_RGBS_ADC2DAC::read() == 0) {
    oldPLLAD = GBS::PLLAD_MD::read();
    oldPLL648_CONTROL_01 = GBS::PLL648_CONTROL_01::read();
    oldPLLAD_ICP = GBS::PLLAD_ICP::read();
    old_0_46 = GBS::RESET_CONTROL_0x46::read();
    old_5_3f = GBS::SP_SDCS_VSST_REG_L::read();
    old_5_40 = GBS::SP_SDCS_VSSP_REG_L::read();
    old_5_3e = GBS::SP_CS_0x3E::read();

    // WIP
    //GBS::PLLAD_ICP::write(5);
    GBS::OUT_SYNC_SEL::write(2); // S0_4F, 6+7
    GBS::DAC_RGBS_ADC2DAC::write(1); // S0_4B, 2
    GBS::SP_HS_LOOP_SEL::write(0); // retiming enable
    //GBS::SP_CS_0x3E::write(0x20); // sub coast off, ovf protect off
    //GBS::PLL648_CONTROL_01::write(0x35); // display clock
    //GBS::PLLAD_MD::write(1802);
    GBS::SP_SDCS_VSST_REG_L::write(0);
    GBS::SP_SDCS_VSSP_REG_L::write(12);
    //GBS::RESET_CONTROL_0x46::write(0); // none required
  }
  else {
    GBS::OUT_SYNC_SEL::write(0);
    GBS::DAC_RGBS_ADC2DAC::write(0);
    GBS::SP_HS_LOOP_SEL::write(1);

    if (oldPLLAD_ICP != 0) GBS::PLLAD_ICP::write(oldPLLAD_ICP);
    if (oldPLL648_CONTROL_01 != 0) GBS::PLL648_CONTROL_01::write(oldPLL648_CONTROL_01);
    if (oldPLLAD != 0) GBS::PLLAD_MD::write(oldPLLAD);
    if (old_5_3e != 0) GBS::SP_CS_0x3E::write(old_5_3e);
    if (old_5_3f != 0) GBS::SP_SDCS_VSST_REG_L::write(old_5_3f);
    if (old_5_40 != 0) GBS::SP_SDCS_VSSP_REG_L::write(old_5_40);
    if (old_0_46 != 0) GBS::RESET_CONTROL_0x46::write(old_0_46);
  }
}

void bypassModeSwitch_RGBHV() {
  writeProgramArrayNew(ntsc_240p); // have a baseline
  
  rto->videoStandardInput = 15; // making sure
  rto->autoBestHtotalEnabled = false; // not necessary, since VDS is off / bypassed
  rto->phaseADC = 16;
  rto->phaseSP = 15;

  GBS::DAC_RGBS_PWDNZ::write(0); // disable DAC

  GBS::PLL_CKIS::write(0); // 0_40 0 //  0: PLL uses OSC clock | 1: PLL uses input clock
  GBS::PLL_DIVBY2Z::write(0); // 0_40 // 1= no divider (full clock, ie 27Mhz) 0 = halved clock
  GBS::PLL_R::write(2); // PLL lock detector skew
  GBS::PLL_S::write(2);
  GBS::PLL_MS::write(2); // select feedback clock (but need to enable tri state!)
  GBS::PAD_TRI_ENZ::write(1); // enable some pad's tri state (they become high-z / inputs), helps noise
  GBS::MEM_PAD_CLK_INVERT::write(0); // helps also
  GBS::PLL648_CONTROL_01::write(0x35);
  GBS::DAC_RGBS_ADC2DAC::write(1);
  GBS::OUT_SYNC_SEL::write(2); // S0_4F, 6+7 | 0x10, H/V sync output from sync processor | 00 from vds_proc
  
  GBS::SP_SOG_SRC_SEL::write(0); // 5_20 0 | 0: from ADC 1: hs is sog source // useless in this mode
  GBS::ADC_SOGEN::write(1); // 5_02 bit 0 // rgbhv bypass test: sog mode // having it off loads the HS line???
  GBS::SP_CLAMP_MANUAL::write(1); // needs to be 1
  GBS::SP_SYNC_BYPS::write(1); // use external (H+V) sync for decimator (and sync out?) 1 to mirror in sync
  GBS::SP_HS_LOOP_SEL::write(0); // 5_57_6 | 0 enables retiming (required to fix short out sync pulses + any inversion)
  GBS::SP_SOG_MODE::write(0); // 5_56 bit 0 // rgbhv bypass test: sog mode
  GBS::SP_CLP_SRC_SEL::write(1); // clamp source 1: pixel clock, 0: 27mhz // rgbhv bypass test: sog mode (unset before)
  GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input ( 5_20 bit 3 )
  GBS::SP_HS2PLL_INV_REG::write(1); // rgbhv general test, seems more stable
  GBS::SP_H_PROTECT::write(0); // 5_3e 4
  GBS::SP_DIS_SUB_COAST::write(1); // 5_3e 5
  GBS::SP_NO_COAST_REG::write(1);
  GBS::SP_PRE_COAST::write(0);
  GBS::SP_POST_COAST::write(0);
  GBS::ADC_CLK_ICLK2X::write(0); // oversampling 1x (off)
  GBS::ADC_CLK_ICLK1X::write(0);
  GBS::PLLAD_KS::write(1); // 0 - 3
  GBS::PLLAD_CKOS::write(0); // 0 - 3
  GBS::DEC1_BYPS::write(1); // 1 = bypassed
  GBS::DEC2_BYPS::write(1);
  GBS::ADC_FLTR::write(0);

  GBS::PLLAD_ICP::write(6);
  GBS::PLLAD_FS::write(1); // high gain
  GBS::PLLAD_MD::write(1856); // 1349 perfect for for 1280x+ ; 1856 allows lower res to detect
  delay(100);
  resetDigital(); // this will leave 0_46 reset controls with 0 (bypassed blocks disabled)
  resetPLLAD();
  delay(20);
  GBS::DAC_RGBS_PWDNZ::write(1); // enable DAC
  delay(10);
  setPhaseSP();
  setPhaseADC();
  togglePhaseAdjustUnits();
  delay(100);
  GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out
  delay(100);
}

void doAutoGain() {
  static uint8_t r_found = 0, g_found = 0, b_found = 0;
  static uint8_t r_off_found = 0, g_off_found = 0, b_off_found = 0;
  static unsigned long lastTime = millis();

  if (millis() - lastTime > 60) {
    r_found = 0; g_found = 0; b_found = 0;
    r_off_found = 0; g_off_found = 0; b_off_found = 0;
    lastTime = millis();
  }

  GBS::DEC_TEST_SEL::write(3); // 0xbc
  uint8_t redValue = GBS::TEST_BUS_2E::read();
  GBS::DEC_TEST_SEL::write(2); // 0xac
  uint8_t greenValue = GBS::TEST_BUS_2E::read();
  GBS::DEC_TEST_SEL::write(1); // 0x9c
  uint8_t blueValue = GBS::TEST_BUS_2E::read();

  // red
  if (redValue == 0x7f) { // full on found
    r_found++;
  }
  else if (redValue == 0) { // black found
    r_off_found++;
  }

  // green
  if (greenValue == 0x7f) {
    g_found++;
  }
  else if (greenValue == 0) {
    g_off_found++;
  }

  // blue
  if (blueValue == 0x7f) {
    b_found++;
  }
  else if (blueValue == 0) {
    b_off_found++;
  }

  if (r_found > 1) {
    if (GBS::ADC_RGCTRL::read() < 0x90) {
      GBS::ADC_RGCTRL::write(GBS::ADC_RGCTRL::read() + r_found);
      //SerialM.print("Rgain: "); SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
      r_found = 0;
    }
  }
  if (g_found > 1) {
    if (GBS::ADC_GGCTRL::read() < 0x90) {
      GBS::ADC_GGCTRL::write(GBS::ADC_GGCTRL::read() + g_found);
      //SerialM.print("Ggain: "); SerialM.println(GBS::ADC_GGCTRL::read(), HEX);
      g_found = 0;
    }
  }
  if (b_found > 1) {
    if (GBS::ADC_BGCTRL::read() < 0x90) {
      GBS::ADC_BGCTRL::write(GBS::ADC_BGCTRL::read() + b_found);
      //SerialM.print("Bgain: "); SerialM.println(GBS::ADC_BGCTRL::read(), HEX);
      b_found = 0;
    }
  }

  // disabled for now
  //if (r_off_found > 2) {
  //  if (GBS::ADC_ROFCTRL::read() > 0x3b) {
  //    GBS::ADC_ROFCTRL::write(GBS::ADC_ROFCTRL::read() - 1);
  //    //SerialM.print("Roffs: "); SerialM.println(GBS::ADC_ROFCTRL::read(), HEX);
  //    r_off_found = 0;
  //  }
  //}
  //if (g_off_found > 2) {
  //  if (GBS::ADC_GOFCTRL::read() > 0x3b) {
  //    GBS::ADC_GOFCTRL::write(GBS::ADC_GOFCTRL::read() - 1);
  //    //SerialM.print("Goffs: "); SerialM.println(GBS::ADC_GOFCTRL::read(), HEX);
  //    g_off_found = 0;
  //  }
  //}
  //if (b_off_found > 2) {
  //  if (GBS::ADC_BOFCTRL::read() > 0x3b) {
  //    GBS::ADC_BOFCTRL::write(GBS::ADC_BOFCTRL::read() - 1);
  //    //SerialM.print("Boffs: "); SerialM.println(GBS::ADC_BOFCTRL::read(), HEX);
  //    b_off_found = 0;
  //  }
  //}
}

void startWire() {
  Wire.begin();
  // The i2c wire library sets pullup resistors on by default. Disable this so that 5V MCUs aren't trying to drive the 3.3V bus.
#if defined(ESP8266)
  pinMode(SCL, OUTPUT_OPEN_DRAIN);
  pinMode(SDA, OUTPUT_OPEN_DRAIN);
  Wire.setClock(100000); // TV5725 supports 400kHz // but 100kHz is better suited
#else
  digitalWrite(SCL, LOW);
  digitalWrite(SDA, LOW);
  Wire.setClock(100000);
#endif
}

void setup() {
  rto->webServerEnabled = true; // control gbs-control(:p) via web browser, only available on wifi boards.
  rto->webServerStarted = false; // make sure this is set
#if defined(ESP8266)
  // SDK enables WiFi and uses stored credentials to auto connect. This can't be turned off.
  // Correct the hostname while it is still in CONNECTING state
  //wifi_station_set_hostname("gbscontrol"); // SDK version
  WiFi.hostname("gbscontrol");

  // start web services as early in boot as possible > greater chance to get a websocket connection in time for logging startup
  if (rto->webServerEnabled) {
    startWebserver();
    WiFi.setOutputPower(14.0f); // float: min 0.0f, max 20.5f // reduced from max, but still strong
    rto->webServerStarted = true;
    unsigned long initLoopStart = millis();
    while (millis() - initLoopStart < 2000) {
      persWM.handleWiFi();
      dnsServer.processNextRequest();
      server.handleClient();
      webSocket.loop();
      delay(1); // allow some time for the ws server to find clients currently trying to reconnect
    }
  }
  else {
    //WiFi.disconnect(); // deletes credentials
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1);
  }
#endif

  Serial.begin(115200); // set Arduino IDE Serial Monitor to the same 115200 bauds!
  Serial.setTimeout(10);
  Serial.println("starting");
  // user options // todo: could be stored in Arduino EEPROM. Other MCUs have SPIFFS
  uopt->presetPreference = 0; // normal, 720p, fb, custom, 1280x1024
  uopt->presetGroup = 0; //
  uopt->enableFrameTimeLock = 0; // permanently adjust frame timing to avoid glitch vertical bar. does not work on all displays!
  uopt->frameTimeLockMethod = 0; // compatibility with more displays
  uopt->enableAutoGain = 0; // todo: web ui option
  // run time options
  rto->allowUpdatesOTA = false; // ESP over the air updates. default to off, enable via web interface
  rto->enableDebugPings = false;
  rto->webSocketConnected = false;
  rto->autoBestHtotalEnabled = true;  // automatically find the best horizontal total pixel value for a given input timing
  rto->syncLockFailIgnore = 2; // allow syncLock to fail x-1 times in a row before giving up (sync glitch immunity)
  rto->forceRetime = false;
  rto->syncWatcherEnabled = true;  // continously checks the current sync status. required for normal operation
  rto->phaseADC = 16;
  rto->phaseSP = 15;

  // the following is just run time variables. don't change!
  rto->inputIsYpBpR = false;
  rto->videoStandardInput = 0;
  rto->outModePassThroughWithIf = false;
  rto->deinterlacerWasTurnedOff = false;
  if (!rto->webServerEnabled) rto->webServerStarted = false;
  rto->printInfos = false;
  rto->sourceDisconnected = true;
  rto->isInLowPowerMode = false;
  rto->applyPresetDoneStage = 0;
  rto->applyPresetDoneTime = millis();
  rto->sourceVLines = 0;
  rto->clampPositionIsSet = 0;
  rto->coastPositionIsSet = 0;
  rto->continousStableCounter = 0;

  globalCommand = 0; // web server uses this to issue commands

  pinMode(DEBUG_IN_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  LEDON; // enable the LED, lets users know the board is starting up
  delay(200); // give the entire system some time to start up.

#if defined(ESP8266)
  //Serial.setDebugOutput(true); // if you want simple wifi debug info
  // file system (web page, custom presets, ect)
  if (!SPIFFS.begin()) {
    SerialM.println("SPIFFS Mount Failed");
  }
  else {
    // load userprefs.txt
    File f = SPIFFS.open("/userprefs.txt", "r");
    if (!f) {
      SerialM.println("userprefs open failed");
      uopt->presetPreference = 0;
      uopt->enableFrameTimeLock = 0;
      uopt->presetGroup = 0;
      uopt->frameTimeLockMethod = 0;
      saveUserPrefs(); // if this fails, there must be a spiffs problem
    }
    else {
      SerialM.println("userprefs open ok");
      //on a fresh / spiffs not formatted yet MCU:
      //userprefs.txt open ok //result[0] = 207 //result[1] = 207
      char result[4];
      result[0] = f.read(); result[0] -= '0'; // file streams with their chars..
      uopt->presetPreference = (uint8_t)result[0];
      SerialM.print("presetPreference = "); SerialM.println(uopt->presetPreference);
      if (uopt->presetPreference > 4) uopt->presetPreference = 0; // fresh spiffs ?

      result[1] = f.read(); result[1] -= '0';
      uopt->enableFrameTimeLock = (uint8_t)result[1]; // Frame Time Lock
      SerialM.print("FrameTime Lock = "); SerialM.println(uopt->enableFrameTimeLock);
      if (uopt->enableFrameTimeLock > 1) uopt->enableFrameTimeLock = 0; // fresh spiffs ?

      result[2] = f.read(); result[2] -= '0';
      uopt->presetGroup = (uint8_t)result[2];
      SerialM.print("presetGroup = "); SerialM.println(uopt->presetGroup); // custom preset group
      if (uopt->presetGroup > 4) uopt->presetGroup = 0;

      result[3] = f.read(); result[3] -= '0';
      uopt->frameTimeLockMethod = (uint8_t)result[3];
      SerialM.print("frameTimeLockMethod = "); SerialM.println(uopt->frameTimeLockMethod);
      if (uopt->frameTimeLockMethod > 1) uopt->frameTimeLockMethod = 0;

      f.close();
    }
  }
#else
  delay(500); // give the entire system some time to start up.
#endif
  startWire();

  // i2c can get stuck
  if (digitalRead(SDA) == 0) {
    unsigned long timeout = millis();
    while (millis() - timeout <= 10000) {
      if (digitalRead(SDA) == 0) {
        static uint8_t result = 0;
        static boolean printDone = 0;
        static uint8_t counter = 0;
        if (!printDone) {
          SerialM.print("i2c: ");
          printDone = 1;
        }
        if (counter > 70) {
          counter = 0;
          SerialM.println("");
        }
        pinMode(SCL, INPUT); pinMode(SDA, INPUT);
        delay(100);
        pinMode(SCL, OUTPUT);
        for (int i = 0; i < 10; i++) {
          digitalWrite(SCL, HIGH); delayMicroseconds(5);
          digitalWrite(SCL, LOW); delayMicroseconds(5);
        }
        pinMode(SCL, INPUT);
        startWire();
        writeOneByte(0xf0, 0); readFromRegister(0x0c, 1, &result);
        SerialM.print(result, HEX);
        // also keep web ui stuff going
        handleWiFi(); // ESP8266 check, WiFi + OTA updates, checks for server enabled + started
        counter++;
      }
      else {
        break;
      }
    }
    SerialM.print("\n");
    if (millis() - timeout > 10000) {
      // never got to see a pulled up SDA. Scaler board is probably not powered
      // or SDA cable not connected
      SerialM.println("\nCheck SDA, SCL connection! Check GBS for power!");
      // don't reboot, go into loop instead (allow for config changes via web ui)
      rto->syncWatcherEnabled = false;
    }
  }

  zeroAll();
  GBS::TEST_BUS_EN::write(0); // to init some template variables
  GBS::TEST_BUS_SEL::write(0);
  
  rto->currentLevelSOG = 4;
  setAndUpdateSogLevel(rto->currentLevelSOG);
  setResetParameters();
  loadPresetMdSection(); // fills 1_60 to 1_83 (mode detect segment, mostly static)
  setAdcParametersGainAndOffset();
  setSpParameters();
  delay(60); // let everything settle

  //rto->syncWatcherEnabled = false;
  //inputAndSyncDetect();

  // allows passive operation by disabling syncwatcher
  if (rto->syncWatcherEnabled == true) {
    rto->isInLowPowerMode = true; // just for initial detection; simplifies code later
    for (uint8_t i = 0; i < 3; i++) {
      if (inputAndSyncDetect()) {
        break;
      }
    }
    rto->isInLowPowerMode = false;
  }
  LEDOFF; // new behaviour: only light LED on active sync
}

#ifdef HAVE_BUTTONS
#define INPUT_SHIFT 0
#define DOWN_SHIFT 1
#define UP_SHIFT 2
#define MENU_SHIFT 3

static const uint8_t historySize = 32;
static const uint16_t buttonPollInterval = 100; // microseconds
static uint8_t buttonHistory[historySize];
static uint8_t buttonIndex;
static uint8_t buttonState;
static uint8_t buttonChanged;

uint8_t readButtons(void) {
  return ~((digitalRead(INPUT_PIN) << INPUT_SHIFT) |
    (digitalRead(DOWN_PIN) << DOWN_SHIFT) |
    (digitalRead(UP_PIN) << UP_SHIFT) |
    (digitalRead(MENU_PIN) << MENU_SHIFT));
}

void debounceButtons(void) {
  buttonHistory[buttonIndex++ % historySize] = readButtons();
  buttonChanged = 0xFF;
  for (uint8_t i = 0; i < historySize; ++i)
    buttonChanged &= buttonState ^ buttonHistory[i];
  buttonState ^= buttonChanged;
}

bool buttonDown(uint8_t pos) {
  return (buttonState & (1 << pos)) && (buttonChanged & (1 << pos));
}

void handleButtons(void) {
  debounceButtons();
  if (buttonDown(INPUT_SHIFT))
    Menu::run(MenuInput::BACK);
  if (buttonDown(DOWN_SHIFT))
    Menu::run(MenuInput::DOWN);
  if (buttonDown(UP_SHIFT))
    Menu::run(MenuInput::UP);
  if (buttonDown(MENU_SHIFT))
    Menu::run(MenuInput::FORWARD);
}
#endif

void handleWiFi() {
#if defined(ESP8266)
  if (rto->webServerEnabled && rto->webServerStarted) {
    persWM.handleWiFi(); // if connected, returns instantly. otherwise it reconnects or opens AP
    dnsServer.processNextRequest();
    server.handleClient();
    webSocket.loop();
    // if there's a control command from the server, globalCommand will now hold it.
    // process it in the parser, then reset to 0 at the end of the sketch.
  }

  if (rto->allowUpdatesOTA) {
    ArduinoOTA.handle();
  }
#endif
}

void loop() {
  static uint8_t readout = 0;
  static uint8_t segment = 0;
  static uint8_t inputRegister = 0;
  static uint8_t inputToogleBit = 0;
  static uint8_t inputStage = 0;
  static uint16_t noSyncCounter = 0;
  static unsigned long lastTimeSyncWatcher = millis();
  static unsigned long lastVsyncLock = millis();
  static unsigned long lastTimeSourceCheck = millis();
#ifdef HAVE_BUTTONS
  static unsigned long lastButton = micros();
#endif

  handleWiFi(); // ESP8266 check, WiFi + OTA updates, checks for server enabled + started

#ifdef HAVE_BUTTONS
  if (micros() - lastButton > buttonPollInterval) {
    lastButton = micros();
    handleButtons();
  }
#endif

  if (Serial.available() || globalCommand != 0) {
    switch (globalCommand == 0 ? Serial.read() : globalCommand) {
    case ' ':
      // skip spaces
      inputStage = 0; // reset this as well
    break;
    case 'd':
      for (int segment = 0; segment <= 5; segment++) {
        dumpRegisters(segment);
      }
      SerialM.println("};");
    break;
    case '+':
      SerialM.println("hor. +");
      shiftHorizontalRight();
      //shiftHorizontalRightIF(4);
    break;
    case '-':
      SerialM.println("hor. -");
      shiftHorizontalLeft();
      //shiftHorizontalLeftIF(4);
    break;
    case '*':
      shiftVerticalUpIF();
    break;
    case '/':
      shiftVerticalDownIF();
    break;
    case 'z':
      SerialM.println("scale+");
      scaleHorizontalLarger();
    break;
    case 'h':
      SerialM.println("scale-");
      scaleHorizontalSmaller();
    break;
    case 'q':
      resetDigital();
      //enableVDS();
    break;
    case 'D':
      //debug stuff: shift h blanking into good view, enhance noise
      if (GBS::ADC_ROFCTRL_FAKE::read() == 0x00) { // "remembers" debug view 
        //shiftHorizontal(700, false); // only do noise for now
        GBS::VDS_PK_Y_H_BYPS::write(0); // enable peaking
        GBS::VDS_PK_LB_CORE::write(0); // peaking high pass open
        GBS::VDS_PK_VL_HL_SEL::write(0);
        GBS::VDS_PK_VL_HH_SEL::write(0);
        GBS::VDS_PK_VH_HL_SEL::write(0);
        GBS::VDS_PK_VH_HH_SEL::write(0);
        GBS::ADC_FLTR::write(0); // 150Mhz / off
        GBS::ADC_ROFCTRL_FAKE::write(GBS::ADC_ROFCTRL::read()); // backup
        GBS::ADC_GOFCTRL_FAKE::write(GBS::ADC_GOFCTRL::read());
        GBS::ADC_BOFCTRL_FAKE::write(GBS::ADC_BOFCTRL::read());
        GBS::ADC_ROFCTRL::write(GBS::ADC_ROFCTRL::read() - 0x32);
        GBS::ADC_GOFCTRL::write(GBS::ADC_GOFCTRL::read() - 0x32);
        GBS::ADC_BOFCTRL::write(GBS::ADC_BOFCTRL::read() - 0x32);
      }
      else {
        //shiftHorizontal(700, true);
        GBS::VDS_PK_Y_H_BYPS::write(1);
        GBS::VDS_PK_LB_CORE::write(1);
        GBS::VDS_PK_VL_HL_SEL::write(1);
        GBS::VDS_PK_VL_HH_SEL::write(1);
        GBS::VDS_PK_VH_HL_SEL::write(1);
        GBS::VDS_PK_VH_HH_SEL::write(1);
        GBS::ADC_FLTR::write(3); // 40Mhz / full
        GBS::ADC_ROFCTRL::write(GBS::ADC_ROFCTRL_FAKE::read()); // restore ..
        GBS::ADC_GOFCTRL::write(GBS::ADC_GOFCTRL_FAKE::read());
        GBS::ADC_BOFCTRL::write(GBS::ADC_BOFCTRL_FAKE::read());
        GBS::ADC_ROFCTRL_FAKE::write(0); // .. and clear
        GBS::ADC_GOFCTRL_FAKE::write(0);
        GBS::ADC_BOFCTRL_FAKE::write(0);
      }
    break;
    case 'C':
      SerialM.println("PLL: ICLK");
      GBS::PLL_MS::write(0); // required again (108Mhz)
      //GBS::MEM_ADR_DLY_REG::write(0x03); GBS::MEM_CLK_DLY_REG::write(0x03); // memory subtimings
      GBS::PLLAD_FS::write(1); // gain high
      GBS::PLLAD_ICP::write(3); // CPC was 5, but MD works with as low as 0 and it removes a glitch
      GBS::PLL_CKIS::write(1); // PLL use ICLK (instead of oscillator)
      latchPLLAD();
      GBS::VDS_HSCALE::write(512);
      rto->syncLockFailIgnore = 2;
      //FrameSync::reset(); // adjust htotal to new display clock
      //rto->forceRetime = true;
      applyBestHTotal(FrameSync::init()); // adjust htotal to new display clock
      applyBestHTotal(FrameSync::init()); // twice
      //GBS::VDS_FLOCK_EN::write(1); //risky
      delay(100);
    break;
    case 'Y':
      writeProgramArrayNew(ntsc_1280x720);
      doPostPresetLoadSteps();
    break;
    case 'y':
      writeProgramArrayNew(pal_1280x720);
      doPostPresetLoadSteps();
    break;
    case 'P':
      //
    break;
    case 'p':
      //
    break;
    case 'k':
      bypassModeSwitch_RGBHV();
      //bypassModeSwitch_SOG();  // arduino space saving
    break;
    case 'K':
      passThroughWithIfModeSwitch();
    break;
    case 'T':
      SerialM.print("auto gain ");
      if (uopt->enableAutoGain == 0 && !rto->inputIsYpBpR) {
        uopt->enableAutoGain = 1;
        GBS::ADC_RGCTRL::write(0x40);
        GBS::ADC_GGCTRL::write(0x40);
        GBS::ADC_BGCTRL::write(0x40);
        /*GBS::ADC_ROFCTRL::write(0x43);
        GBS::ADC_GOFCTRL::write(0x43);
        GBS::ADC_BOFCTRL::write(0x43);*/
        GBS::DEC_TEST_ENABLE::write(1);
        SerialM.println("on");
      }
      else {
        uopt->enableAutoGain = 0;
        GBS::DEC_TEST_ENABLE::write(0);
        SerialM.println("off");
      }
    break;
    case 'e':
      writeProgramArrayNew(ntsc_240p);
      doPostPresetLoadSteps();
    break;
    case 'r':
      writeProgramArrayNew(pal_240p);
      doPostPresetLoadSteps();
    break;
    case '.':
      // timings recalculation with new bestHtotal
      FrameSync::reset();
      rto->syncLockFailIgnore = 2;
      rto->forceRetime = true;
    break;
    case 'j':
      resetPLL(); latchPLLAD(); //resetPLLAD();
    break;
    case 'v':
      rto->phaseSP += 1; rto->phaseSP &= 0x1f;
      SerialM.print("SP: "); SerialM.println(rto->phaseSP);
      setPhaseSP();
      //setPhaseADC();
    break;
    case 'b':
      advancePhase(); latchPLLAD();
      SerialM.print("ADC: "); SerialM.println(rto->phaseADC);
    break;
    case 'B':
      movePhaseThroughRange();
    break;
    case 'n':
    {
      uint16_t pll_divider = GBS::PLLAD_MD::read();
      if (pll_divider < 4095) {
        pll_divider += 1;
        GBS::PLLAD_MD::write(pll_divider);
        // regular output modes: apply IF corrections
        if (GBS::VDS_HSYNC_RST::read() != 0xfff) {
          uint16_t IF_HSYNC_RST = GBS::IF_HSYNC_RST::read(); // 1_0E
          GBS::IF_HSYNC_RST::write(IF_HSYNC_RST + 1);
          // IF HB new stuff
          GBS::IF_INI_ST::write(IF_HSYNC_RST - 1); // initial position seems to be "ht" (on S1_0d)
          //GBS::IF_LINE_ST::write(GBS::IF_LINE_ST::read() + 1);
          GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() + 1); // 1_22
        }
        latchPLLAD();
        applyBestHTotal(GBS::VDS_HSYNC_RST::read());
        SerialM.print("PLL div: "); SerialM.println(pll_divider, HEX);
        rto->clampPositionIsSet = false;
        rto->coastPositionIsSet = false;
      }
    }
    break;
    case 'a':
      applyBestHTotal(GBS::VDS_HSYNC_RST::read() + 1);
      SerialM.print("HTotal++: "); SerialM.println(GBS::VDS_HSYNC_RST::read());
    break;
    case 'A':
      //optimizeSogLevel();
      //optimizePhaseSP();
      applyBestHTotal(GBS::VDS_HSYNC_RST::read() - 1);
      SerialM.print("HTotal--: "); SerialM.println(GBS::VDS_HSYNC_RST::read());
    break;
    case 'M':
      zeroAll();
    break;
    case 'm':
      SerialM.print("syncwatcher ");
      if (rto->syncWatcherEnabled == true) {
        rto->syncWatcherEnabled = false;
        SerialM.println("off");
      }
      else {
        rto->syncWatcherEnabled = true;
        SerialM.println("on");
      }
    break;
    case ',':
#if defined(ESP8266) // Arduino space saving
      getVideoTimings();
#endif
    break;
    case 'i':
      rto->printInfos = !rto->printInfos;
    break;
#if defined(ESP8266)
    case 'c':
      SerialM.println("OTA Updates on");
      initUpdateOTA();
      rto->allowUpdatesOTA = true;
    break;
    case 'G':
      SerialM.print("Debug Pings ");
      if (!rto->enableDebugPings) {
        SerialM.println("on");
        rto->enableDebugPings = 1;
      }
      else {
        SerialM.println("off");
        rto->enableDebugPings = 0;
      }
    break;
#endif
    case 'u':
      ResetSDRAM();
    break;
    case 'f':
      SerialM.print("peaking ");
      if (GBS::VDS_PK_Y_H_BYPS::read() == 1) {
        GBS::VDS_PK_Y_H_BYPS::write(0);
        SerialM.println("on");
      }
      else {
        GBS::VDS_PK_Y_H_BYPS::write(1);
        SerialM.println("off");
      }
    break;
    case 'F':
      SerialM.print("ADC filter ");
      if (GBS::ADC_FLTR::read() > 0) {
        GBS::ADC_FLTR::write(0);
        SerialM.println("off");
      }
      else {
        GBS::ADC_FLTR::write(3);
        SerialM.println("on");
      }
    break;
    case 'L':
      //
    break;
    case 'l':
      SerialM.println("resetSyncProcessor");
      //disableDeinterlacer();
      resetSyncProcessor();
      //delay(10);
      //enableDeinterlacer();
    break;
    case 'W':
      uopt->enableFrameTimeLock = !uopt->enableFrameTimeLock;
    break;
    case 'E':
      //
    break;
    case '0':
      moveHS(1, true);
    break;
    case '1':
      moveHS(1, false);
    break;
    case '2':
      writeProgramArrayNew(pal_feedbackclock); // ModeLine "720x576@50" 27 720 732 795 864 576 581 586 625 -hsync -vsync
      doPostPresetLoadSteps();
    break;
    case '3':
      //
      break;
    case '4':
      scaleVertical(1, true);
    break;
    case '5':
      scaleVertical(1, false);
    break;
    case '6':
      GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() - 4); // canvas move left
    break;
    case '7':
      GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() + 4); // canvas move right
    break;
    case '8':
      //SerialM.println("invert sync");
      invertHS(); invertVS();
    break;
    case '9':
      writeProgramArrayNew(ntsc_feedbackclock);
      doPostPresetLoadSteps();
    break;
    case 'o':
    {
      switch (GBS::PLLAD_CKOS::read()) {
      case 0:
        SerialM.println("OSR 1x"); // oversampling ratio
        GBS::ADC_CLK_ICLK2X::write(0);
        GBS::ADC_CLK_ICLK1X::write(0);
        GBS::PLLAD_KS::write(2); // 0 - 3
        GBS::PLLAD_CKOS::write(2); // 0 - 3
        GBS::DEC1_BYPS::write(1); // 1 = bypassed
        GBS::DEC2_BYPS::write(1);
        break;
      case 1:
        SerialM.println("OSR 4x");
        GBS::ADC_CLK_ICLK2X::write(1);
        GBS::ADC_CLK_ICLK1X::write(1);
        GBS::PLLAD_KS::write(2); // 0 - 3
        GBS::PLLAD_CKOS::write(0); // 0 - 3
        GBS::DEC1_BYPS::write(0);
        GBS::DEC2_BYPS::write(0);
        break;
      case 2:
        SerialM.println("OSR 2x");
        GBS::ADC_CLK_ICLK2X::write(0);
        GBS::ADC_CLK_ICLK1X::write(1);
        GBS::PLLAD_KS::write(2); // 0 - 3
        GBS::PLLAD_CKOS::write(1); // 0 - 3
        GBS::DEC1_BYPS::write(1);
        GBS::DEC2_BYPS::write(0);
        break;
      default:
        break;
      }
      //resetPLLAD(); // just latching not good enough, shifts h offset
      //ResetSDRAM(); // sdram sometimes locks up going from x4 to x1
      // test!
      latchPLLAD();
    }
    break;
    case 'g':
      inputStage++;
      Serial.flush();
      // we have a multibyte command
      if (inputStage > 0) {
        if (inputStage == 1) {
          segment = Serial.parseInt();
          SerialM.print("G");
          SerialM.print(segment);
        }
        else if (inputStage == 2) {
          char szNumbers[3];
          szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
          //char * pEnd;
          inputRegister = strtol(szNumbers, NULL, 16);
          SerialM.print("R0x");
          SerialM.print(inputRegister, HEX);
          if (segment <= 5) {
            writeOneByte(0xF0, segment);
            readFromRegister(inputRegister, 1, &readout);
            SerialM.print(" value: 0x"); SerialM.println(readout, HEX);
          }
          else {
            SerialM.println("abort");
          }
          inputStage = 0;
        }
      }
    break;
    case 's':
      inputStage++;
      Serial.flush();
      // we have a multibyte command
      if (inputStage > 0) {
        if (inputStage == 1) {
          segment = Serial.parseInt();
          SerialM.print("S");
          SerialM.print(segment);
        }
        else if (inputStage == 2) {
          char szNumbers[3];
          szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
          //char * pEnd;
          inputRegister = strtol(szNumbers, NULL, 16);
          SerialM.print("R0x");
          SerialM.print(inputRegister, HEX);
        }
        else if (inputStage == 3) {
          char szNumbers[3];
          szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
          //char * pEnd;
          inputToogleBit = strtol(szNumbers, NULL, 16);
          if (segment <= 5) {
            writeOneByte(0xF0, segment);
            readFromRegister(inputRegister, 1, &readout);
            SerialM.print(" (was 0x"); SerialM.print(readout, HEX); SerialM.print(")");
            writeOneByte(inputRegister, inputToogleBit);
            readFromRegister(inputRegister, 1, &readout);
            SerialM.print(" is now: 0x"); SerialM.println(readout, HEX);
          }
          else {
            SerialM.println("abort");
          }
          inputStage = 0;
        }
      }
    break;
    case 't':
      inputStage++;
      Serial.flush();
      // we have a multibyte command
      if (inputStage > 0) {
        if (inputStage == 1) {
          segment = Serial.parseInt();
          SerialM.print("T");
          SerialM.print(segment);
        }
        else if (inputStage == 2) {
          char szNumbers[3];
          szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
          //char * pEnd;
          inputRegister = strtol(szNumbers, NULL, 16);
          SerialM.print("R0x");
          SerialM.print(inputRegister, HEX);
        }
        else if (inputStage == 3) {
          inputToogleBit = Serial.parseInt();
          SerialM.print(" Bit: "); SerialM.print(inputToogleBit);
          inputStage = 0;
          if ((segment <= 5) && (inputToogleBit <= 7)) {
            writeOneByte(0xF0, segment);
            readFromRegister(inputRegister, 1, &readout);
            SerialM.print(" (was 0x"); SerialM.print(readout, HEX); SerialM.print(")");
            writeOneByte(inputRegister, readout ^ (1 << inputToogleBit));
            readFromRegister(inputRegister, 1, &readout);
            SerialM.print(" is now: 0x"); SerialM.println(readout, HEX);
          }
          else {
            SerialM.println("abort");
          }
        }
      }
    break;
    case 'w':
    {
      Serial.flush();
      uint16_t value = 0;
      String what = Serial.readStringUntil(' ');

      if (what.length() > 5) {
        SerialM.println("abort");
        inputStage = 0;
        break;
      }
      value = Serial.parseInt();
      if (value < 4096) {
        SerialM.print("set "); SerialM.print(what); SerialM.print(" "); SerialM.println(value);
        if (what.equals("ht")) {
          set_htotal(value);
          //applyBestHTotal(value);
        }
        else if (what.equals("vt")) {
          set_vtotal(value);
        }
        else if (what.equals("hsst")) {
          setHSyncStartPosition(value);
        }
        else if (what.equals("hssp")) {
          setHSyncStopPosition(value);
        }
        else if (what.equals("hbst")) {
          setMemoryHblankStartPosition(value);
        }
        else if (what.equals("hbsp")) {
          setMemoryHblankStopPosition(value);
        }
        else if (what.equals("hbstd")) {
          setDisplayHblankStartPosition(value);
        }
        else if (what.equals("hbspd")) {
          setDisplayHblankStopPosition(value);
        }
        else if (what.equals("vsst")) {
          setVSyncStartPosition(value);
        }
        else if (what.equals("vssp")) {
          setVSyncStopPosition(value);
        }
        else if (what.equals("vbst")) {
          setMemoryVblankStartPosition(value);
        }
        else if (what.equals("vbsp")) {
          setMemoryVblankStopPosition(value);
        }
        else if (what.equals("vbstd")) {
          setDisplayVblankStartPosition(value);
        }
        else if (what.equals("vbspd")) {
          setDisplayVblankStopPosition(value);
        }
        else if (what.equals("sog")) {
          setAndUpdateSogLevel(value);
        }
      }
      else {
        SerialM.println("abort");
      }
    }
    break;
    case 'x':
    {
      uint16_t if_hblank_scale_stop = GBS::IF_HBIN_SP::read();
      GBS::IF_HBIN_SP::write(if_hblank_scale_stop + 1);
      SerialM.print("1_26: "); SerialM.println(if_hblank_scale_stop + 1);
    }
    break;
    default:
      SerialM.println("unknown command");
      break;
    }
    // a web ui or terminal command has finished. good idea to reset sync lock timer
    // important if the command was to change presets, possibly others
    lastVsyncLock = millis();
  }
  globalCommand = 0; // in case the web server had this set

  // run FrameTimeLock if enabled
  if (uopt->enableFrameTimeLock && rto->sourceDisconnected == false && rto->autoBestHtotalEnabled && 
    rto->syncWatcherEnabled && FrameSync::ready() && millis() - lastVsyncLock > FrameSyncAttrs::lockInterval
    && rto->continousStableCounter > 5) {
    
    uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
    if (debug_backup != 0x0) {
      GBS::TEST_BUS_SEL::write(0x0);
    }
    if (!FrameSync::run(uopt->frameTimeLockMethod)) {
      if (rto->syncLockFailIgnore-- == 0) {
        FrameSync::reset(); // in case run() failed because we lost a sync signal
      }
    }
    else if (rto->syncLockFailIgnore > 0) {
      rto->syncLockFailIgnore = 2;
    }
    if (debug_backup != 0x0) {
      GBS::TEST_BUS_SEL::write(debug_backup);
    }
    lastVsyncLock = millis();
  }

  if (rto->printInfos == true) { // information mode
    static uint8_t runningNumber = 0;
    uint8_t lockCounter = 0;
    static uint8_t buffer[99] = { 0 };

    uint8_t stat0 = GBS::STATUS_00::read();
    uint8_t stat5 = GBS::STATUS_05::read();
    uint8_t video_mode = getVideoMode();
    uint8_t adc_gain_r = GBS::ADC_RGCTRL::read();
    uint8_t adc_gain_g = GBS::ADC_GGCTRL::read();
    uint8_t adc_gain_b = GBS::ADC_BGCTRL::read();
    uint16_t HPERIOD_IF = (rto->videoStandardInput == 15) ? 0: GBS::HPERIOD_IF::read();
    uint16_t VPERIOD_IF = (rto->videoStandardInput == 15) ? 0: GBS::VPERIOD_IF::read();
    uint16_t TEST_BUS = GBS::TEST_BUS::read();
    uint16_t STATUS_SYNC_PROC_HTOTAL = GBS::STATUS_SYNC_PROC_HTOTAL::read();
    uint16_t STATUS_SYNC_PROC_VTOTAL = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    uint16_t STATUS_SYNC_PROC_HLOW_LEN = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
    boolean STATUS_MISC_PLL648_LOCK = GBS::STATUS_MISC_PLL648_LOCK::read();
    runningNumber++;
    if (runningNumber == 99) { runningNumber = 0; }
    buffer[runningNumber] = GBS::STATUS_MISC_PLLAD_LOCK::read();
    for (uint8_t i = 0; i < 99; i++) {
      lockCounter += buffer[i];
    }
    //lockDisplay /= 10;
    //if (STATUS_MISC_PLLAD_LOCK) { runningLocks++; }
    
    String dbg = TEST_BUS < 0x10 ? "000" + String(TEST_BUS, HEX) : TEST_BUS < 0x100 ? "00" + String(TEST_BUS, HEX) : TEST_BUS < 0x1000 ? "0" + String(TEST_BUS, HEX) : String(TEST_BUS, HEX);
    String hpw = STATUS_SYNC_PROC_HLOW_LEN < 100 ? "00" + String(STATUS_SYNC_PROC_HLOW_LEN) : STATUS_SYNC_PROC_HLOW_LEN < 1000 ? "0" + String(STATUS_SYNC_PROC_HLOW_LEN) : String(STATUS_SYNC_PROC_HLOW_LEN);
    String lockDisplay = lockCounter < 10 ? "0" + String(lockCounter) : String(lockCounter);
    String stableCounter = String(rto->continousStableCounter, HEX);

    String output = "h:" + String(HPERIOD_IF) + " " + "v:" + String(VPERIOD_IF) + " PLL" +
      (STATUS_MISC_PLL648_LOCK ? "." : "x") + lockDisplay + 
      " A:" + String(adc_gain_r, HEX) + String(adc_gain_g, HEX) + String(adc_gain_b, HEX) +
      " S:" + String(stat0, HEX) + String(".") + String(stat5, HEX) +
      " D:" + dbg + " m:" + String(video_mode) + " ht:" + String(STATUS_SYNC_PROC_HTOTAL) +
      " vt:" + String(STATUS_SYNC_PROC_VTOTAL) + " hpw:" + hpw + " s:" + stableCounter 
#if defined(ESP8266)
      +String(" W:") + String(WiFi.RSSI())
#endif
      ;

    SerialM.println(output);
  } // end information mode

  // test
  handleWiFi(); // ESP8266 check, WiFi + OTA updates, checks for server enabled + started

  // syncwatcher polls SP status. when necessary, initiates adjustments or preset changes
  if (rto->sourceDisconnected == false && rto->syncWatcherEnabled == true 
    && (millis() - lastTimeSyncWatcher) > 20) 
  {
    uint8_t newVideoMode = getVideoMode();
    if ((!getSyncStable() || newVideoMode == 0) && rto->videoStandardInput != 15) {
      noSyncCounter++;
      rto->continousStableCounter = 0;
      LEDOFF; // always LEDOFF on sync loss, except if RGBHV
      disableDeinterlacer(); // engage free run for VDS ("coasting"), helps displays keep sync
      if (rto->printInfos == false) {
        if (noSyncCounter == 1) {
          SerialM.print(".");
        }
      }
      if (noSyncCounter % 40 == 0) {
        enableDeinterlacer(); // briefly show image
        rto->clampPositionIsSet = false;
        rto->coastPositionIsSet = false;
        FrameSync::reset(); // corner case: source quickly changed. this won't affect display if timings are the same
        SerialM.print("!");
      }
    }
    else if (rto->videoStandardInput != 15){
      LEDON;
    }
    // if format changed to valid
    if ((newVideoMode != 0 && newVideoMode != rto->videoStandardInput && getSyncStable()) ||
      (newVideoMode != 0 && rto->videoStandardInput == 0 /*&& getSyncPresent()*/)) {
      noSyncCounter = 0;
      uint8_t test = 10;
      uint8_t changeToPreset = newVideoMode;
      uint8_t signalInputChangeCounter = 0;

      // this first test is necessary with "dirty" sync (CVid)
      while (--test > 0) { // what's the new preset?
        delay(2);
        newVideoMode = getVideoMode();
        if (changeToPreset == newVideoMode) {
          signalInputChangeCounter++;
        }
      }
      if (signalInputChangeCounter >= 8) { // video mode has changed
        SerialM.println("New Input");
        uint8_t timeout = 255;
        while (newVideoMode == 0 && --timeout > 0) {
          newVideoMode = getVideoMode();
          delay(1); // rarely needed but better than not
        }
        if (timeout > 0) {
          // going to apply the new preset now
          boolean wantPassThroughMode = (rto->outModePassThroughWithIf == 1 && rto->videoStandardInput <= 2);
          if (!wantPassThroughMode) {
            applyPresets(newVideoMode);
          }
          else
          {
            if (newVideoMode <= 2) {
              // is in PT, is SD
              latchPLLAD(); // then this is enough
            }
            else {
              applyPresets(newVideoMode); // we were in PT and new input is HD
            }
          }

          rto->videoStandardInput = newVideoMode;
          delay(20); // only a brief delay
        }
        else {
          SerialM.println(" .. lost");
        }
        noSyncCounter = 0;
      }
    }
    else if (getSyncStable() && newVideoMode != 0 && rto->videoStandardInput != 15) { // last used mode reappeared / stable again
      if (rto->continousStableCounter < 255) {
        rto->continousStableCounter++;
      }
      noSyncCounter = 0;
      if (rto->deinterlacerWasTurnedOff) {
        enableDeinterlacer();
      }
    }

    if (rto->videoStandardInput == 15) { // RGBHV checks
      static uint8_t RGBHVNoSyncCounter = 0;
      uint8_t VSHSStatus = GBS::STATUS_16::read();

      if ((VSHSStatus & 0x0a) != 0x0a) {
        LEDOFF;
        RGBHVNoSyncCounter++;
        rto->continousStableCounter = 0;
        if (RGBHVNoSyncCounter % 20 == 0) {
          SerialM.print("!");
        }
      }
      else {
        RGBHVNoSyncCounter = 0;
        LEDON;
        if (rto->continousStableCounter < 255) {
          rto->continousStableCounter++;
        }
      }

      if (RGBHVNoSyncCounter > 200) {
          RGBHVNoSyncCounter = 0;
          setResetParameters();
          setSpParameters();
          resetSyncProcessor(); // todo: fix MD being stuck in last mode when sync disappears
          resetModeDetect();
          noSyncCounter = 0;
      }

      static unsigned long lastTimeCheck = millis();

      if (rto->continousStableCounter > 3 && (GBS::STATUS_MISC_PLLAD_LOCK::read() != 1)
        && (millis() - lastTimeCheck > 750)) 
      {
        // RGBHV PLLAD optimization by looking at he PLLAD lock counter
        uint8_t lockCounter = 0;
        for (uint8_t i = 0; i < 10; i++) {
          lockCounter += GBS::STATUS_MISC_PLLAD_LOCK::read();
          delay(1);
        }
        if (lockCounter < 9) {
          LEDOFF;
          static uint8_t toggle = 0;
          if (toggle < 7) {
            GBS::PLLAD_ICP::write((GBS::PLLAD_ICP::read() + 1) & 0x07);
            toggle++;
          }
          else {
            static uint8_t lowRun = 0;
            GBS::PLLAD_ICP::write(4); // restart a little higher
            GBS::PLLAD_FS::write(!GBS::PLLAD_FS::read());
            if (lowRun > 1) {
              GBS::PLLAD_MD::write(1349); // should also enable the 30Mhz ADC filter
              rto->clampPositionIsSet = false;
              //updateClampPosition();
              if (lowRun == 3) {
                lowRun = 0;
              }
            }
            else {
              GBS::PLLAD_MD::write(1856);
              rto->clampPositionIsSet = false;
              //updateClampPosition();
            }
            lowRun++;
            toggle = 0;
            delay(10);
          }
          latchPLLAD();
          delay(30);
          //setPhaseSP(); setPhaseADC();
          togglePhaseAdjustUnits();
          SerialM.print("> "); 
          SerialM.print(GBS::PLLAD_MD::read());
          SerialM.print(": "); SerialM.print(GBS::PLLAD_ICP::read());
          SerialM.print(":"); SerialM.println(GBS::PLLAD_FS::read());
          delay(200);
          // invert HPLL trigger?
          uint16_t STATUS_SYNC_PROC_VTOTAL = GBS::STATUS_SYNC_PROC_VTOTAL::read();
          boolean SP_HS2PLL_INV_REG = GBS::SP_HS2PLL_INV_REG::read();
          if (SP_HS2PLL_INV_REG && 
            ((STATUS_SYNC_PROC_VTOTAL >= 626 && STATUS_SYNC_PROC_VTOTAL <= 628) || // 800x600
            (STATUS_SYNC_PROC_VTOTAL >= 523 && STATUS_SYNC_PROC_VTOTAL <= 525)))   // 640x480
          { 
            GBS::SP_HS2PLL_INV_REG::write(0);
          }
          else if (!SP_HS2PLL_INV_REG) {
            GBS::SP_HS2PLL_INV_REG::write(1);
          }
          lastTimeCheck = millis();
        }
      }
    }

    if (noSyncCounter >= 40) { // attempt fixes
      if (getSyncPresent() && rto->videoStandardInput != 15) { // only if there's at least a signal
        if (rto->inputIsYpBpR && noSyncCounter == 40) {
          GBS::SP_NO_CLAMP_REG::write(1); // unlock clamp
          rto->coastPositionIsSet = false;
          rto->clampPositionIsSet = false;
          delay(10);
        }
        if ((noSyncCounter % 120) == 0) {
          //optimizeSogLevel();  // todo: optimize optimizeSogLevel()
          setAndUpdateSogLevel(rto->currentLevelSOG / 2);
          SerialM.print("SOG: "); SerialM.println(rto->currentLevelSOG);
        }
        if (noSyncCounter % 40 == 0) {
          static boolean toggle = rto->videoStandardInput > 2 ? 0 : 1;
          if (toggle) {
            SerialM.print("HD? ");
            GBS::SP_H_PULSE_IGNOR::write(0x04);
            GBS::SP_PRE_COAST::write(0x06);
            GBS::SP_POST_COAST::write(0x07);
          }
          else {
            SerialM.print("SD? ");
            GBS::SP_H_PULSE_IGNOR::write(0x10);
            GBS::SP_PRE_COAST::write(6); // SP test: 9
            GBS::SP_POST_COAST::write(16); // SP test: 9
          }
          toggle = !toggle;
        }
      }

      // couldn't recover, source is lost
      // restore initial conditions and move to input detect
      if (noSyncCounter >= 400) {
        disableDeinterlacer();
        setResetParameters();
        setSpParameters();
        resetSyncProcessor(); // todo: fix MD being stuck in last mode when sync disappears
        resetModeDetect();
        noSyncCounter = 0;
      }
    }

    lastTimeSyncWatcher = millis();
  }

  // frame sync + besthtotal init routine. this only runs if !FrameSync::ready(), ie manual retiming, preset load, etc)
  if (!FrameSync::ready() && rto->continousStableCounter > 6 && rto->syncWatcherEnabled == true
    && rto->autoBestHtotalEnabled == true
    && rto->videoStandardInput != 0 && rto->videoStandardInput != 15)
  {
    uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
    if (debug_backup != 0x0) {
      GBS::TEST_BUS_SEL::write(0x0);
    }
    //GBS::VDS_FLOCK_EN::write(1);
    uint16_t bestHTotal = FrameSync::init();
    if (bestHTotal > 0) {
      if (bestHTotal >= 4095) bestHTotal = 4095;
      applyBestHTotal(bestHTotal);
      rto->syncLockFailIgnore = 2;
    }
    else if (rto->syncLockFailIgnore-- == 0) {
      // frame time lock failed, most likely due to missing wires
      rto->autoBestHtotalEnabled = false;
      SerialM.println("lock failed, check debug wire!");
    }
    if (debug_backup != 0x0) {
      GBS::TEST_BUS_SEL::write(debug_backup);
    }
    //GBS::VDS_FLOCK_EN::write(0);
    if (GBS::PAD_SYNC_OUT_ENZ::read()) { // if 1 > sync still off
      GBS::PAD_SYNC_OUT_ENZ::write(0); // output sync > display goes on
    }
  }
  
  // update clamp + coast positions after preset change // do it quickly
  if (!rto->coastPositionIsSet && rto->continousStableCounter > 10) {
      updateCoastPosition();
  }
  if (!rto->clampPositionIsSet && rto->continousStableCounter > 10) {
    updateClampPosition();
  }
  
  // need to reset ModeDetect shortly after loading a new preset
  if (rto->applyPresetDoneStage > 0 && 
    ((millis() - rto->applyPresetDoneTime < 5000)) && 
    ((millis() - rto->applyPresetDoneTime > 500))) 
  {
    // todo: why is auto clamp failing unless MD is being reset manually?
    // the 4 is chosen to do quickly skip this, if possible
    if (rto->applyPresetDoneStage == 1) {
      // manual preset changes with syncwatcher disabled will leave clamp off, so use the chance to engage it
      if (!rto->syncWatcherEnabled) { updateClampPosition(); }
      resetModeDetect();
      delay(300);
      //SerialM.println("reset MD");
      rto->applyPresetDoneStage = 0;
    }

    // update: don't do this automatically anymore. It only really applies to the 1Chip SNES, so "261" lines should
    // be dealt with as a special condition, not the other way around

    // if this is not a custom preset AND offset has not yet been applied
    //if (rto->applyPresetDoneStage == 2 && rto->continousStableCounter > 40) 
    //{
    //  if (GBS::ADC_0X00_RESERVED_5::read() != 1 && GBS::IF_AUTO_OFST_RESERVED_2::read() != 1) {
    //    if (rto->videoStandardInput == 1) { // only 480i for now (PAL seems to be fine without)
    //      rto->sourceVLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    //      SerialM.print("vlines: "); SerialM.println(rto->sourceVLines);
    //      if (rto->sourceVLines > 263 && rto->sourceVLines <= 274)
    //      {
    //        GBS::IF_VB_SP::write(GBS::IF_VB_SP::read() + 16);
    //        GBS::IF_VB_ST::write(GBS::IF_VB_SP::read() - 1);
    //        GBS::IF_AUTO_OFST_RESERVED_2::write(1); // mark as already adjusted
    //      }
    //    }
    //    //delay(50);
    //    rto->applyPresetDoneStage = 0;
    //  }
    //  else {
    //    rto->applyPresetDoneStage = 0;
    //  }
    //}
  }
  else if (rto->applyPresetDoneStage > 0 && (millis() - rto->applyPresetDoneTime > 5000)) {
    rto->applyPresetDoneStage = 0; // timeout
  }

  if (rto->syncWatcherEnabled == true && rto->sourceDisconnected == true) {
    if ((millis() - lastTimeSourceCheck) > 1000) {
      inputAndSyncDetect(); // source is off; keep looking for new input
      lastTimeSourceCheck = millis();
    }
  }

#if defined(ESP8266) // no more space on ATmega
  // run auto ADC gain feature (if enabled)
  if (rto->syncWatcherEnabled && uopt->enableAutoGain == 1 && !rto->sourceDisconnected 
    && rto->videoStandardInput > 0 && rto->continousStableCounter > 80 && rto->clampPositionIsSet
    && !rto->inputIsYpBpR) // only works for RGB atm
  {
    uint8_t debugRegBackup = 0, debugPinBackup = 0;
    debugPinBackup = GBS::PAD_BOUT_EN::read();
    debugRegBackup = GBS::TEST_BUS_SEL::read();
    GBS::PAD_BOUT_EN::write(0); // disable output to pin for test
    GBS::TEST_BUS_SEL::write(0xb); // decimation
    //GBS::DEC_TEST_ENABLE::write(1);
    doAutoGain();
    //GBS::DEC_TEST_ENABLE::write(0);
    GBS::TEST_BUS_SEL::write(debugRegBackup);
    GBS::PAD_BOUT_EN::write(debugPinBackup); // debug output pin back on
  }

#ifdef HAVE_PINGER_LIBRARY
  // periodic pings for debugging WiFi issues
  static unsigned long pingLastTime = millis();
  if (rto->enableDebugPings && millis() - pingLastTime > 500) {
    if (pinger.Ping(WiFi.gatewayIP(), 1, 499) == false)
    {
      Serial.println("Error during last ping command.");
    }
    pingLastTime = millis();
  }
#endif
#endif
}

#if defined(ESP8266)

void handleRoot() {
  // server.send_P allows directly sending from PROGMEM, using less RAM. It can stall however
  // server.send uses a String held in RAM. Some stats:
  // root start heap: 32928
  // page in ram, heap: 22584
  // page sent, heap: 24888
  webSocket.disconnect();
  String page = FPSTR(HTML);
  //server.sendHeader("Content-Length", String(page.length())); // library already does this
  server.send(200, "text/html", page);
  //server.send_P(200, "text/html", HTML); // send_P method, no String needed
  server.client().stop(); // not sure
}

void handleType1Command() {
  server.send(200);
  server.client().stop(); // not sure
  if (server.hasArg("plain")) {
    globalCommand = server.arg("plain").charAt(0);
  }
}

void handleType2Command() {
  server.send(200);
  server.client().stop(); // not sure
  if (server.hasArg("plain")) {
    char argument = server.arg("plain").charAt(0);
    switch (argument) {
    case '0':
      uopt->presetPreference = 0; // normal
      saveUserPrefs();
      break;
    case '1':
      uopt->presetPreference = 1; // prefer fb clock
      saveUserPrefs();
      break;
    case '2':
      uopt->presetPreference = 4; // prefer 1280x1024 preset
      saveUserPrefs();
      break;
    case '3':  // load custom preset
    {
      if (rto->videoStandardInput == 0) SerialM.println("no input detected, aborting action");
      else {
        const uint8_t* preset = loadPresetFromSPIFFS(rto->videoStandardInput); // load for current video mode
        writeProgramArrayNew(preset);
        doPostPresetLoadSteps();
      }
    }
    break;
    case '4':
      if (rto->videoStandardInput == 0) SerialM.println("no input detected, aborting action");
      else savePresetToSPIFFS();
      break;
    case '5':
      //Frame Time Lock ON
      uopt->enableFrameTimeLock = 1;
      saveUserPrefs();
      break;
    case '6':
      //Frame Time Lock OFF
      uopt->enableFrameTimeLock = 0;
      saveUserPrefs();
      break;
    case '7':
    {
      // scanline toggle
      uopt->enableAutoGain = 0; // incompatible
      uint8_t reg;
      writeOneByte(0xF0, 2);
      readFromRegister(0x16, 1, &reg);
      if ((reg & 0x80) == 0x80) {
        writeOneByte(0x16, reg ^ (1 << 7));
        GBS::ADC_RGCTRL::write(GBS::ADC_RGCTRL::read() - 0x1c);
        GBS::ADC_GGCTRL::write(GBS::ADC_GGCTRL::read() - 0x1c);
        GBS::ADC_BGCTRL::write(GBS::ADC_BGCTRL::read() - 0x1c);
        writeOneByte(0xF0, 3);
        writeOneByte(0x35, 0xd0); // more luma gain
        writeOneByte(0xF0, 2);
        writeOneByte(0x27, 0x28); // set up VIIR filter (no need to undo later)
        writeOneByte(0x26, 0x00);
      }
      else {
        writeOneByte(0x16, reg ^ (1 << 7));
        GBS::ADC_RGCTRL::write(GBS::ADC_RGCTRL::read() + 0x1c);
        GBS::ADC_GGCTRL::write(GBS::ADC_GGCTRL::read() + 0x1c);
        GBS::ADC_BGCTRL::write(GBS::ADC_BGCTRL::read() + 0x1c);
        writeOneByte(0xF0, 3);
        writeOneByte(0x35, 0x80);
        writeOneByte(0xF0, 2);
        writeOneByte(0x26, 0x40); // disables VIIR filter
      }
    }
    break;
    case '9':
      uopt->presetPreference = 3; // prefer 720p preset
      saveUserPrefs();
      break;
    case 'a':
      // restart ESP MCU (due to an SDK bug, this does not work reliably after programming. 
      // It needs a power cycle or reset button push first.)
      SerialM.println("Restarting MCU");
      ESP.restart();
      break;
    case 'b':
      uopt->presetGroup = 0;
      uopt->presetPreference = 2; // custom
      saveUserPrefs();
      break;
    case 'c':
      uopt->presetGroup = 1;
      uopt->presetPreference = 2;
      saveUserPrefs();
      break;
    case 'd':
      uopt->presetGroup = 2;
      uopt->presetPreference = 2;
      saveUserPrefs();
      break;
    case 'j':
      uopt->presetGroup = 3;
      uopt->presetPreference = 2;
      saveUserPrefs();
      break;
    case 'k':
      uopt->presetGroup = 4;
      uopt->presetPreference = 2;
      saveUserPrefs();
      break;
    case 'e': // print files on spiffs
    {
      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
        SerialM.print(dir.fileName()); SerialM.print(" "); SerialM.println(dir.fileSize());
        delay(1); // wifi stack
      }
      ////
      File f = SPIFFS.open("/userprefs.txt", "r");
      if (!f) {
        SerialM.println("userprefs open failed");
      }
      else {
        char result[4];
        result[0] = f.read(); result[0] -= '0'; // file streams with their chars..
        SerialM.print("presetPreference = "); SerialM.println((uint8_t)result[0]);
        result[1] = f.read(); result[1] -= '0';
        SerialM.print("FrameTime Lock = "); SerialM.println((uint8_t)result[1]);
        result[2] = f.read(); result[2] -= '0';
        SerialM.print("presetGroup = "); SerialM.println((uint8_t)result[2]);
        result[3] = f.read(); result[3] -= '0';
        SerialM.print("frameTimeLockMethod = "); SerialM.println((uint8_t)result[3]);
        f.close();
      }
    }
    break;
    case 'f':
    {
      // load 1280x960 preset via webui
      uint8_t videoMode = getVideoMode();
      if (videoMode == 0) videoMode = rto->videoStandardInput; // last known good as fallback
      uint8_t backup = uopt->presetPreference;
      uopt->presetPreference = 0; // override RAM copy of presetPreference for applyPresets
      rto->videoStandardInput = 0; // force hard reset
      applyPresets(videoMode);
      uopt->presetPreference = backup;
    }
    break;
    case 'g':
    {
      // load 1280x720 preset via webui
      uint8_t videoMode = getVideoMode();
      if (videoMode == 0) videoMode = rto->videoStandardInput;
      uint8_t backup = uopt->presetPreference;
      uopt->presetPreference = 3;
      rto->videoStandardInput = 0; // force hard reset
      applyPresets(videoMode);
      uopt->presetPreference = backup;
    }
    break;
    case 'h':
    {
      // load 640x480 preset via webui
      uint8_t videoMode = getVideoMode();
      if (videoMode == 0) videoMode = rto->videoStandardInput;
      uint8_t backup = uopt->presetPreference;
      uopt->presetPreference = 1;
      rto->videoStandardInput = 0; // force hard reset
      applyPresets(videoMode);
      uopt->presetPreference = backup;
    }
    break;
    case 'p':
    {
      // load 1280x1024
      uint8_t videoMode = getVideoMode();
      if (videoMode == 0) videoMode = rto->videoStandardInput;
      uint8_t backup = uopt->presetPreference;
      uopt->presetPreference = 4;
      rto->videoStandardInput = 0; // force hard reset
      applyPresets(videoMode);
      uopt->presetPreference = backup;
    }
    break;
    case 'i':
      // toggle active frametime lock method
      if (uopt->frameTimeLockMethod == 0) uopt->frameTimeLockMethod = 1;
      else if (uopt->frameTimeLockMethod == 1) uopt->frameTimeLockMethod = 0;
      saveUserPrefs();
      break;
    case 'l':
      // cycle through available SDRAM clocks
    {
      uint8_t PLL_MS = GBS::PLL_MS::read();
      uint8_t memClock = 0;
      PLL_MS++; PLL_MS &= 0x7;
      switch (PLL_MS) {
      case 0: memClock = 108; break;
      case 1: memClock = 81; break; // goes well with 4_2C = 0x14, 4_2D = 0x27
      case 2: memClock = 10; break; //feedback clock
      case 3: memClock = 162; break;
      case 4: memClock = 144; break;
      case 5: memClock = 185; break;
      case 6: memClock = 216; break;
      case 7: memClock = 129; break;
      default: break;
      }
      GBS::PLL_MS::write(PLL_MS);
      ResetSDRAM();
      if (memClock != 10) {
        SerialM.print("SDRAM clock: "); SerialM.print(memClock); SerialM.println("Mhz");
      }
      else {
        SerialM.print("SDRAM clock: "); SerialM.println("Feedback clock (default)");
      }
    }
    break;
    case 'm':
      // DCTI (pixel edges slope enhancement)
      if (GBS::VDS_UV_STEP_BYPS::read() == 1) {
        GBS::VDS_UV_STEP_BYPS::write(0);
        // VDS_TAP6_BYPS (S3_24, 3) no longer enabled by default
        /*if (GBS::VDS_TAP6_BYPS::read() == 1) {
          GBS::VDS_TAP6_BYPS::write(0); // no good way to store this change for later reversal
          GBS::VDS_0X2A_RESERVED_2BITS::write(1); // so use this trick to detect it later
          }*/
        SerialM.println("DCTI on");
      }
      else {
        GBS::VDS_UV_STEP_BYPS::write(1);
        /*if (GBS::VDS_0X2A_RESERVED_2BITS::read() == 1) {
          GBS::VDS_TAP6_BYPS::write(1);
          GBS::VDS_0X2A_RESERVED_2BITS::write(0);
          }*/
        SerialM.println("DCTI off");
      }
      break;
    case 'n':
      SerialM.print("ADC gain++ : ");
      GBS::ADC_RGCTRL::write(GBS::ADC_RGCTRL::read() - 1);
      GBS::ADC_GGCTRL::write(GBS::ADC_GGCTRL::read() - 1);
      GBS::ADC_BGCTRL::write(GBS::ADC_BGCTRL::read() - 1);
      SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
      break;
    case 'o':
      SerialM.print("ADC gain-- : ");
      GBS::ADC_RGCTRL::write(GBS::ADC_RGCTRL::read() + 1);
      GBS::ADC_GGCTRL::write(GBS::ADC_GGCTRL::read() + 1);
      GBS::ADC_BGCTRL::write(GBS::ADC_BGCTRL::read() + 1);
      SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
      break;
    case 'A':
      GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - 4);
      GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() + 4);
      break;
    case 'B':
      GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() + 4);
      GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() - 4);
      break;
    case 'C':
      GBS::VDS_DIS_VB_ST::write(GBS::VDS_DIS_VB_ST::read() - 2);
      GBS::VDS_DIS_VB_SP::write(GBS::VDS_DIS_VB_SP::read() + 2);
      break;
    case 'D':
      GBS::VDS_DIS_VB_ST::write(GBS::VDS_DIS_VB_ST::read() + 2);
      GBS::VDS_DIS_VB_SP::write(GBS::VDS_DIS_VB_SP::read() - 2);
      break;
    default:
      break;
    }
  }
}

void startWebserver()
{
  //WiFi.disconnect(); // test captive portal by forgetting wifi credentials
  persWM.setApCredentials(ap_ssid, ap_password);
  persWM.onConnect([]() {
    WiFi.hostname("gbscontrol");
    SerialM.print("local IP: ");
    SerialM.println(WiFi.localIP());
    SerialM.print("hostname: "); SerialM.println(WiFi.hostname());
  });
  persWM.onAp([]() {
    WiFi.hostname("gbscontrol");
    SerialM.print("AP MODE: "); SerialM.println("connect to wifi network called gbscontrol with password qqqqqqqq");
  });

  server.on("/", handleRoot);
  server.on("/serial_", handleType1Command);
  server.on("/user_", handleType2Command);

  persWM.setConnectNonBlock(true);
  persWM.begin(); // WiFiManager with captive portal
  MDNS.begin("gbscontrol"); // respnd to MDNS request for gbscontrol.local
  server.begin(); // Webserver for the site
  webSocket.begin();  // Websocket for interaction
#ifdef HAVE_PINGER_LIBRARY
  // pinger library
  pinger.OnReceive([](const PingerResponse& response)
  {
    if (response.ReceivedResponse)
    {
      SerialM.printf(
        "Reply from %s: time=%lums TTL=%d\n",
        response.DestIPAddress.toString().c_str(),
        response.ResponseTime,
        response.TimeToLive);
    }
    else
    {
      SerialM.printf("Request timed out.\n");
    }

    // Return true to continue the ping sequence.
    // If current event returns false, the ping sequence is interrupted.
    return true;
  });

  pinger.OnEnd([](const PingerResponse& response)
  {
    // detailed info not necessary
    return true;
  });
#endif
}

void initUpdateOTA() {
  ArduinoOTA.setHostname("GBS OTA");

  // ArduinoOTA.setPassword("admin");
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    SPIFFS.end();
    SerialM.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    SerialM.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    SerialM.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    SerialM.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) SerialM.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) SerialM.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) SerialM.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) SerialM.println("Receive Failed");
    else if (error == OTA_END_ERROR) SerialM.println("End Failed");
  });
  ArduinoOTA.begin();
}

// sets every element of str to 0 (clears array)
void StrClear(char *str, uint16_t length)
{
  for (int i = 0; i < length; i++) {
    str[i] = 0;
  }
}

const uint8_t* loadPresetFromSPIFFS(byte forVideoMode) {
  static uint8_t preset[496];
  String s = "";
  char group = '0';
  File f;

  f = SPIFFS.open("/userprefs.txt", "r");
  if (f) {
    SerialM.println("userprefs.txt opened");
    char result[3];
    result[0] = f.read(); // todo: move file cursor manually
    result[1] = f.read();
    result[2] = f.read();

    f.close();
    if ((uint8_t)(result[2] - '0') < 10) group = result[2]; // otherwise not stored on spiffs
    SerialM.print("loading from presetGroup "); SerialM.println((uint8_t)(group - '0')); // custom preset group (console)
  }
  else {
    // file not found, we don't know what preset to load
    SerialM.println("please select a preset group first!");
    if (forVideoMode == 2 || forVideoMode == 4) return pal_240p;
    else return ntsc_240p;
  }

  if (forVideoMode == 1) {
    f = SPIFFS.open("/preset_ntsc." + String(group), "r");
  }
  else if (forVideoMode == 2) {
    f = SPIFFS.open("/preset_pal." + String(group), "r");
  }
  else if (forVideoMode == 3) {
    f = SPIFFS.open("/preset_ntsc_480p." + String(group), "r");
  }
  else if (forVideoMode == 4) {
    f = SPIFFS.open("/preset_pal_576p." + String(group), "r");
  }
  else if (forVideoMode == 5) {
    f = SPIFFS.open("/preset_ntsc_720p." + String(group), "r");
  }
  else if (forVideoMode == 6) {
    f = SPIFFS.open("/preset_ntsc_1080p." + String(group), "r");
  }

  if (!f) {
    SerialM.println("open preset file failed");
    if (forVideoMode == 2 || forVideoMode == 4) return pal_240p;
    else return ntsc_240p;
  }
  else {
    SerialM.println("preset file open ok: ");
    SerialM.println(f.name());
    s = f.readStringUntil('}');
    f.close();
  }

  char *tmp;
  uint16_t i = 0;
  tmp = strtok(&s[0], ",");
  while (tmp) {
    preset[i++] = (uint8_t)atoi(tmp);
    tmp = strtok(NULL, ",");
    yield(); // wifi stack
  }

  return preset;
}

void savePresetToSPIFFS() {
  uint8_t readout = 0;
  File f;
  char group = '0';

  // first figure out if the user has set a preferenced group
  f = SPIFFS.open("/userprefs.txt", "r");
  if (f) {
    char result[3];
    result[0] = f.read(); // todo: move file cursor manually
    result[1] = f.read();
    result[2] = f.read();

    f.close();
    group = result[2];
    SerialM.print("saving to presetGroup "); SerialM.println(result[2]); // custom preset group (console)
  }
  else {
    // file not found, we don't know where to save this preset
    SerialM.println("please select a preset group first!");
    return;
  }

  if (rto->videoStandardInput == 1) {
    f = SPIFFS.open("/preset_ntsc." + String(group), "w");
  }
  else if (rto->videoStandardInput == 2) {
    f = SPIFFS.open("/preset_pal." + String(group), "w");
  }
  else if (rto->videoStandardInput == 3) {
    f = SPIFFS.open("/preset_ntsc_480p." + String(group), "w");
  }
  else if (rto->videoStandardInput == 4) {
    f = SPIFFS.open("/preset_pal_576p." + String(group), "w");
  }
  else if (rto->videoStandardInput == 5) {
    f = SPIFFS.open("/preset_ntsc_720p." + String(group), "w");
  }
  else if (rto->videoStandardInput == 6) {
    f = SPIFFS.open("/preset_ntsc_1080p." + String(group), "w");
  }

  if (!f) {
    SerialM.println("open preset file failed");
  }
  else {
    SerialM.println("preset file open ok");

    GBS::ADC_0X00_RESERVED_5::write(1); // use one reserved bit to mark this as a custom preset

    for (int i = 0; i <= 5; i++) {
      writeOneByte(0xF0, i);
      switch (i) {
      case 0:
        for (int x = 0x40; x <= 0x5F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        for (int x = 0x90; x <= 0x9F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      case 1:
        for (int x = 0x0; x <= 0x2F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      case 2:
        for (int x = 0x0; x <= 0x3F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      case 3:
        for (int x = 0x0; x <= 0x7F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      case 4:
        for (int x = 0x0; x <= 0x5F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      case 5:
        for (int x = 0x0; x <= 0x6F; x++) {
          readFromRegister(x, 1, &readout);
          f.print(readout); f.println(",");
        }
        break;
      }
    }
    f.println("};");
    SerialM.print("preset saved as: ");
    SerialM.println(f.name());
    f.close();
  }
}

void saveUserPrefs() {
  File f = SPIFFS.open("/userprefs.txt", "w");
  if (!f) {
    SerialM.println("saving preferences failed");
    return;
  }
  f.write(uopt->presetPreference + '0');
  f.write(uopt->enableFrameTimeLock + '0');
  f.write(uopt->presetGroup + '0');
  f.write(uopt->frameTimeLockMethod + '0');
  SerialM.println("userprefs saved: ");
  f.close();

  // print results
  f = SPIFFS.open("/userprefs.txt", "r");
  if (!f) {
    SerialM.println("userprefs open failed");
  }
  else {
    char result[4];
    result[0] = f.read(); result[0] -= '0'; // file streams with their chars..
    SerialM.print("  presetPreference = "); SerialM.println((uint8_t)result[0]);
    result[1] = f.read(); result[1] -= '0';
    SerialM.print("  FrameTime Lock = "); SerialM.println((uint8_t)result[1]);
    result[2] = f.read(); result[2] -= '0';
    SerialM.print("  presetGroup = "); SerialM.println((uint8_t)result[2]);
    result[3] = f.read(); result[3] -= '0';
    SerialM.print("  frameTimeLockMethod = "); SerialM.println((uint8_t)result[3]);
    f.close();
  }
}

#endif
