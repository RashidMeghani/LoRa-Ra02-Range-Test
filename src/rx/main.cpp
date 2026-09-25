// LoRa Ra-02 range test -- RECEIVER with 1.3" OLED
// Board: Arduino Nano (ATmega328P) + AI-Thinker Ra-02 (SX1278, 433 MHz)
//        + 1.3" SH1106 128x64 I2C OLED
// Libraries (pulled in by platformio.ini):
//   "LoRa" by Sandeep Mistry (v0.8.0+)
//   "U8g2" by oliver (uses the buffer-free U8x8 text mode: the Nano only has
//    2 KB RAM, so a full-frame-buffer OLED library would leave too little)
//
// OLED shows: last RSSI (big), SNR, link margin, received / lost / loss %,
// seconds since last packet, and the weakest RSSI seen. Every packet is also
// printed as a CSV line on Serial (115200) for logging on a laptop.
//
// Wiring (Ra-02 is 3.3 V ONLY -- see README.md for level shifting/power):
//   Ra-02 NSS  -> D10      Ra-02 MOSI -> D11      Ra-02 SCK -> D13
//   Ra-02 MISO -> D12      Ra-02 RST  -> D8       Ra-02 DIO0 -> D9
//   Ra-02 3.3V -> external 3.3 V regulator (NOT the Nano's 3V3 pin)
//   OLED SDA -> A4   OLED SCL -> A5   OLED VCC -> 5V   OLED GND -> GND
//   Optional active buzzer or LED on D4: short beep per received packet, so
//   you can walk away and hear when packets stop arriving.

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <U8x8lib.h>

// ---- Radio settings: MUST be identical on TX and RX ----------------------
#define LORA_FREQ        433E6
#define LORA_SF          12
#define LORA_BW          125E3
#define LORA_CR          8
#define LORA_SYNC_WORD   0x34
#define LORA_PREAMBLE    8

// ---- Pins ----------------------------------------------------------------
#define PIN_LORA_NSS     10
#define PIN_LORA_RST     8
#define PIN_LORA_DIO0    9       // polled, so no interrupt pin needed
#define PIN_BEEP         4

#define BEEP_MS          40
#define SCREEN_REFRESH_MS 500

// 1.3" modules use the SH1106 controller. For a 0.96" (SSD1306) module use
// U8X8_SSD1306_128X64_NONAME_HW_I2C instead.
U8X8_SH1106_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);

// Demodulation SNR floor per spreading factor (SX1278 datasheet), dB.
// Link margin = measured SNR - floor. Near 0 dB the link is about to drop.
const int8_t SNR_FLOOR[] = { -5, -7, -10, -12, -15, -17, -20 };  // SF6..SF12

uint32_t rxCount = 0;
uint32_t lostCount = 0;
uint32_t lastSeq = 0;
int      lastRssi = 0;
float    lastSnr = 0;
int      minRssi = 0;
int      txPower = 0;
long     freqErr = 0;
unsigned long lastRxMs = 0;
unsigned long lastScreenMs = 0;
unsigned long beepOffMs = 0;
bool     haveRx = false;

void printPadded(uint8_t col, uint8_t row, const char *s) {
  // Pad to the end of the 16-char line so old text is overwritten.
  char buf[17];
  uint8_t w = 16 - col;
  uint8_t n = strlen(s);
  if (n > w) n = w;
  memcpy(buf, s, n);
  memset(buf + n, ' ', w - n);
  buf[w] = 0;
  oled.drawString(col, row, buf);
}

void drawScreen() {
  char line[20];
  char f[8];

  if (!haveRx) {
    unsigned long s = millis() / 1000;
    snprintf(line, sizeof(line), "Waiting... %lus", s);
    printPadded(0, 7, line);
    return;
  }

  // Rows 0-1: big RSSI (2x2 font -> 8 chars per line)
  snprintf(line, sizeof(line), "%4d dBm", lastRssi);
  oled.draw2x2String(0, 0, line);

  dtostrf(lastSnr, 5, 1, f);
  snprintf(line, sizeof(line), "SNR%s dB", f);
  printPadded(0, 2, line);

  float margin = lastSnr - SNR_FLOOR[LORA_SF - 6];
  dtostrf(margin, 5, 1, f);
  snprintf(line, sizeof(line), "Margin%s dB", f);
  printPadded(0, 3, line);

  snprintf(line, sizeof(line), "Rx%lu Lost%lu", rxCount, lostCount);
  printPadded(0, 4, line);

  uint32_t total = rxCount + lostCount;
  float loss = total ? (100.0 * lostCount / total) : 0;
  dtostrf(loss, 4, 1, f);
  snprintf(line, sizeof(line), "Loss%s%% #%lu", f, lastSeq);
  printPadded(0, 5, line);

  snprintf(line, sizeof(line), "Min%4d Tx%ddBm", minRssi, txPower);
  printPadded(0, 6, line);

  unsigned long ago = (millis() - lastRxMs) / 1000;
  snprintf(line, sizeof(line), "Last %lus ago", ago);
  printPadded(0, 7, line);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BEEP, OUTPUT);

  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();
  oled.drawString(0, 0, "LoRa Range RX");

  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    oled.drawString(0, 2, "LoRa init FAIL");
    oled.drawString(0, 3, "check wiring");
    Serial.println(F("LoRa init FAILED - check wiring/power"));
    while (true) {}
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setPreambleLength(LORA_PREAMBLE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  LoRa.receive();                        // continuous receive mode

  char line[20];
  snprintf(line, sizeof(line), "%ldMHz SF%d", (long)(LORA_FREQ / 1E6), LORA_SF);
  oled.drawString(0, 2, line);
  snprintf(line, sizeof(line), "BW%ldk CR4/%d", (long)(LORA_BW / 1E3), LORA_CR);
  oled.drawString(0, 3, line);

  Serial.println(F("ms,seq,rssi,snr,margin,freqErrHz,rx,lost"));
}

void loop() {
  int size = LoRa.parsePacket();
  if (size > 0) {
    uint8_t buf[16];
    uint8_t n = 0;
    while (LoRa.available() && n < sizeof(buf)) buf[n++] = LoRa.read();
    while (LoRa.available()) LoRa.read();

    // Only accept our own 7-byte test packets.
    if (n == 7 && buf[0] == 'R' && buf[1] == 'T') {
      uint32_t seq = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8) |
                     ((uint32_t)buf[4] << 16) | ((uint32_t)buf[5] << 24);

      if (!haveRx || seq <= lastSeq) {
        // First packet, or the transmitter was reset: start fresh stats.
        if (haveRx) oled.clear();
        rxCount = 0;
        lostCount = 0;
        minRssi = 0;
      } else {
        lostCount += seq - lastSeq - 1;
      }
      if (!haveRx) oled.clear();

      lastSeq  = seq;
      txPower  = buf[6];
      lastRssi = LoRa.packetRssi();
      lastSnr  = LoRa.packetSnr();
      freqErr  = LoRa.packetFrequencyError();
      if (rxCount == 0 || lastRssi < minRssi) minRssi = lastRssi;
      rxCount++;
      lastRxMs = millis();
      haveRx = true;

      digitalWrite(PIN_BEEP, HIGH);
      beepOffMs = millis() + BEEP_MS;

      Serial.print(lastRxMs);        Serial.print(',');
      Serial.print(seq);             Serial.print(',');
      Serial.print(lastRssi);        Serial.print(',');
      Serial.print(lastSnr, 1);      Serial.print(',');
      Serial.print(lastSnr - SNR_FLOOR[LORA_SF - 6], 1); Serial.print(',');
      Serial.print(freqErr);         Serial.print(',');
      Serial.print(rxCount);         Serial.print(',');
      Serial.println(lostCount);

      drawScreen();
      lastScreenMs = millis();
    }
  }

  if (beepOffMs && (long)(millis() - beepOffMs) >= 0) {
    digitalWrite(PIN_BEEP, LOW);
    beepOffMs = 0;
  }

  // Keep the "last heard" counter ticking even when nothing arrives.
  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    drawScreen();
  }
}
