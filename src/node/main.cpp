// LoRa Ra-02 range test -- TWO-WAY (ping-pong), same firmware on both boards
// Board: Arduino Nano + AI-Thinker Ra-02 (SX1278, 433 MHz)
//        + 1.3" SH1106 128x64 I2C OLED + active buzzer on D4
//
// Build env "nodeA" -> NODE_MASTER=1: sends a PING every PING_INTERVAL_MS.
// Build env "nodeB" -> NODE_MASTER=0: answers every PING with a PONG.
// Only node A starts a transmission, so the two radios never talk over
// each other. Every packet also carries the RSSI/SNR at which the sender
// heard the other node's last packet, so BOTH screens show both directions:
//   local  = how well I hear the other node
//   remote = how well the other node hears me
//
// Wiring (both boards identical, Ra-02 is 3.3 V ONLY):
//   Ra-02 NSS  -> D10 (divider)   Ra-02 MOSI -> D11 (divider)
//   Ra-02 SCK  -> D13 (divider)   Ra-02 MISO -> D12 (direct)
//   Ra-02 RST  -> D8  (divider)   Ra-02 DIO0 -> D9  (direct)
//   OLED SDA -> A4   OLED SCL -> A5   OLED VCC -> 5V
//   Buzzer + -> D4 (or via transistor), buzzer - -> GND

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <U8x8lib.h>

#ifndef NODE_MASTER
#define NODE_MASTER 0
#endif

// ---- Radio settings: MUST be identical on both nodes ---------------------
#define LORA_FREQ        433E6
#define LORA_SF          12
#define LORA_BW          125E3
#define LORA_CR          8
#define LORA_SYNC_WORD   0x34
#define LORA_PREAMBLE    8
#define LORA_TX_POWER    20

// ---- Timing (SF12 packet airtime is ~1.2 s) ------------------------------
#define PING_INTERVAL_MS 5000UL  // node A: time between PINGs
#define PONG_TIMEOUT_MS  3000UL  // node A: wait this long after a PING ends
#define LINK_LOST_MS     (3 * PING_INTERVAL_MS + 1000UL)

// ---- Pins ----------------------------------------------------------------
#define PIN_LORA_NSS     10
#define PIN_LORA_RST     8
#define PIN_LORA_DIO0    9       // polled, so no interrupt pin needed
#define PIN_BEEP         4

#define BEEP_MS          40
#define SCREEN_REFRESH_MS 500

// ---- Packet: 'R' '2' type seq(4, LE) rssi(1) snr(1) = 9 bytes ------------
//   rssi byte: -RSSI of the last packet heard from the other node, 0 = none
//   snr  byte: SNR * 4 as int8
#define PKT_LEN          9
#define PKT_PING         1
#define PKT_PONG         2

// 1.3" modules use the SH1106 controller. For a 0.96" (SSD1306) module use
// U8X8_SSD1306_128X64_NONAME_HW_I2C instead.
U8X8_SH1106_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);

// Demodulation SNR floor per spreading factor (SX1278 datasheet), dB.
const int8_t SNR_FLOOR[] = { -5, -7, -10, -12, -15, -17, -20 };  // SF6..SF12
#define SNR_LIMIT (SNR_FLOOR[LORA_SF - 6])

const char NODE_NAME = NODE_MASTER ? 'A' : 'B';

uint32_t txCount = 0;      // packets I sent
uint32_t rxCount = 0;      // packets I received from the other node
uint32_t lostCount = 0;    // A: PINGs with no PONG; B: gaps in PING seq
uint32_t lastSeq = 0;

bool     haveRx = false;   // heard the other node at least once
int      localRssi = 0;    // how I hear the other node
float    localSnr = 0;
int      minLocal = 0;

bool     haveRemote = false;  // the other node reported how it hears me
int      remoteRssi = 0;
float    remoteSnr = 0;
int      minRemote = 0;

bool     reportValid = false; // my local reading is fresh enough to send

unsigned long lastRxMs = 0;
unsigned long lastScreenMs = 0;
unsigned long beepOffMs = 0;

// Node A only
bool          awaitingPong = false;
uint32_t      pingSeq = 0;
unsigned long pingStartMs = 0;
unsigned long pingEndMs = 0;

// --------------------------------------------------------------------------

void beep() {
  digitalWrite(PIN_BEEP, HIGH);
  beepOffMs = millis() + BEEP_MS;
  if (beepOffMs == 0) beepOffMs = 1;
}

void printPadded(uint8_t row, const char *s) {
  // Pad to the full 16-char line so old text is overwritten.
  char buf[17];
  uint8_t n = strlen(s);
  if (n > 16) n = 16;
  memcpy(buf, s, n);
  memset(buf + n, ' ', 16 - n);
  buf[16] = 0;
  oled.drawString(0, row, buf);
}

void sendPacket(uint8_t type, uint32_t seq) {
  uint8_t r = 0;
  int8_t  s = 0;
  if (reportValid) {
    r = (uint8_t)constrain(-localRssi, 1, 255);
    s = (int8_t)constrain((int)lround(localSnr * 4), -128, 127);
  }
  LoRa.beginPacket();
  LoRa.write('R');
  LoRa.write('2');
  LoRa.write(type);
  LoRa.write((uint8_t)(seq));
  LoRa.write((uint8_t)(seq >> 8));
  LoRa.write((uint8_t)(seq >> 16));
  LoRa.write((uint8_t)(seq >> 24));
  LoRa.write(r);
  LoRa.write((uint8_t)s);
  LoRa.endPacket();                      // blocks until sent (~1.2 s)
  txCount++;
}

void resetStats() {
  if (!NODE_MASTER) txCount = 0;
  rxCount = 0;
  lostCount = 0;
  haveRemote = false;
  oled.clear();
}

void drawScreen() {
  char line[20];
  char f1[8], f2[8];

  if (!haveRx) {
    snprintf(line, sizeof(line), "Tx%lu", txCount);
    printPadded(5, line);
    snprintf(line, sizeof(line), "Waiting... %lus", millis() / 1000);
    printPadded(7, line);
    return;
  }

  // Rows 0-1: how I hear the other node (big)
  snprintf(line, sizeof(line), "%4d dBm", localRssi);
  oled.draw2x2String(0, 0, line);

  dtostrf(localSnr, 5, 1, f1);
  dtostrf(localSnr - SNR_LIMIT, 5, 1, f2);
  snprintf(line, sizeof(line), "SNR%s M%s", f1, f2);
  printPadded(2, line);

  // How the other node hears me
  if (haveRemote) {
    dtostrf(remoteSnr, 5, 1, f1);
    snprintf(line, sizeof(line), "Rem%4d S%s", remoteRssi, f1);
  } else {
    snprintf(line, sizeof(line), "Rem  --");
  }
  printPadded(3, line);

  snprintf(line, sizeof(line), "Tx%lu Rx%lu", txCount, rxCount);
  printPadded(4, line);

  uint32_t total = rxCount + lostCount;
  float loss = total ? (100.0 * lostCount / total) : 0;
  dtostrf(loss, 4, 1, f1);
  snprintf(line, sizeof(line), "Lost%lu %s%%", lostCount, f1);
  printPadded(5, line);

  if (haveRemote) snprintf(line, sizeof(line), "Min%4d/%4d", minLocal, minRemote);
  else            snprintf(line, sizeof(line), "Min%4d/ --", minLocal);
  printPadded(6, line);

  unsigned long ago = (millis() - lastRxMs) / 1000;
  if (millis() - lastRxMs > LINK_LOST_MS)
    snprintf(line, sizeof(line), "%c LOST %lus", NODE_NAME, ago);
  else
    snprintf(line, sizeof(line), "%c Last %lus ago", NODE_NAME, ago);
  printPadded(7, line);
}

void logCsv(uint32_t seq) {
  Serial.print(millis());       Serial.print(',');
  Serial.print(seq);            Serial.print(',');
  Serial.print(localRssi);      Serial.print(',');
  Serial.print(localSnr, 1);    Serial.print(',');
  if (haveRemote) { Serial.print(remoteRssi); Serial.print(','); Serial.print(remoteSnr, 1); }
  else            { Serial.print(','); }
  Serial.print(',');
  Serial.print(txCount);        Serial.print(',');
  Serial.print(rxCount);        Serial.print(',');
  Serial.println(lostCount);
}

void handleRx() {
  int size = LoRa.parsePacket();
  if (size <= 0) return;

  uint8_t buf[16];
  uint8_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = LoRa.read();
  while (LoRa.available()) LoRa.read();
  if (n != PKT_LEN || buf[0] != 'R' || buf[1] != '2') return;

  uint8_t  type = buf[2];
  uint32_t seq  = (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) |
                  ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);

  // Node A listens for PONGs to its current PING; node B for PINGs.
  if (NODE_MASTER) {
    if (type != PKT_PONG || !awaitingPong || seq != pingSeq) return;
    awaitingPong = false;
  } else {
    if (type != PKT_PING) return;
    if (haveRx && seq > lastSeq) lostCount += seq - lastSeq - 1;
    else if (haveRx) resetStats();       // node A was restarted
    lastSeq = seq;
  }

  bool first = !haveRx;
  localRssi = LoRa.packetRssi();
  localSnr  = LoRa.packetSnr();
  if (first || rxCount == 0 || localRssi < minLocal) minLocal = localRssi;
  rxCount++;
  lastRxMs = millis();
  haveRx = true;
  reportValid = true;

  if (buf[7] != 0) {
    remoteRssi = -(int)buf[7];
    remoteSnr  = (int8_t)buf[8] / 4.0;
    if (!haveRemote || remoteRssi < minRemote) minRemote = remoteRssi;
    haveRemote = true;
  }

  // Node B answers straight away; node A's radio is already listening.
  if (!NODE_MASTER) sendPacket(PKT_PONG, seq);

  beep();
  logCsv(seq);
  if (first) oled.clear();
  drawScreen();
  lastScreenMs = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BEEP, OUTPUT);

  oled.setBusClock(400000);
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();
  oled.drawString(0, 0, NODE_MASTER ? "LoRa 2-way: A" : "LoRa 2-way: B");
  oled.drawString(0, 1, NODE_MASTER ? "(sends PING)" : "(answers PONG)");

  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    oled.drawString(0, 3, "LoRa init FAIL");
    oled.drawString(0, 4, "check wiring");
    Serial.println(F("LoRa init FAILED - check wiring/power"));
    while (true) {}
  }

  LoRa.setTxPower(LORA_TX_POWER, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setPreambleLength(LORA_PREAMBLE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();

  char line[20];
  snprintf(line, sizeof(line), "%ldMHz SF%d", (long)(LORA_FREQ / 1E6), LORA_SF);
  oled.drawString(0, 3, line);
  snprintf(line, sizeof(line), "BW%ldk %ddBm", (long)(LORA_BW / 1E3), LORA_TX_POWER);
  oled.drawString(0, 4, line);

  Serial.print(F("Node ")); Serial.println(NODE_NAME);
  Serial.println(F("ms,seq,rssi,snr,remRssi,remSnr,tx,rx,lost"));
}

void loop() {
  handleRx();

#if NODE_MASTER
  if (awaitingPong && millis() - pingEndMs > PONG_TIMEOUT_MS) {
    awaitingPong = false;
    lostCount++;
    reportValid = false;               // don't resend a stale reading
    Serial.print(F("no reply to PING ")); Serial.println(pingSeq);
  }
  if (!awaitingPong && (pingSeq == 0 || millis() - pingStartMs >= PING_INTERVAL_MS)) {
    pingStartMs = millis();
    pingSeq++;
    sendPacket(PKT_PING, pingSeq);
    pingEndMs = millis();
    awaitingPong = true;
  }
#endif

  if (beepOffMs && (long)(millis() - beepOffMs) >= 0) {
    digitalWrite(PIN_BEEP, LOW);
    beepOffMs = 0;
  }

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    drawScreen();
  }
}
