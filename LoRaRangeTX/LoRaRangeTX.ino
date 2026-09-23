// LoRa Ra-02 range test -- TRANSMITTER
// Board: Arduino Nano (ATmega328P) + AI-Thinker Ra-02 (SX1278, 433 MHz)
// Library: "LoRa" by Sandeep Mistry (v0.8.0+), install via Library Manager.
//
// Sends a small numbered packet every TX_INTERVAL_MS using the longest-range
// LoRa settings (SF12 / 125 kHz / CR 4/8 / +20 dBm on PA_BOOST).
// The receiver uses the sequence number to count lost packets.
//
// Wiring (Ra-02 is 3.3 V ONLY -- see README.md for level shifting/power):
//   Ra-02 NSS  -> D10      Ra-02 MOSI -> D11      Ra-02 SCK -> D13
//   Ra-02 MISO -> D12      Ra-02 RST  -> D8       Ra-02 DIO0 -> D9
//   Ra-02 3.3V -> external 3.3 V regulator (NOT the Nano's 3V3 pin)
//   Ra-02 GND  -> GND
//   Optional LED (+330R) on D4, lights while transmitting.
//
// NEVER power the Ra-02 and transmit without the antenna connected.

#include <SPI.h>
#include <LoRa.h>

// ---- Radio settings: MUST be identical on TX and RX ----------------------
#define LORA_FREQ        433E6   // Ra-02 band: 410-525 MHz. Check local rules.
#define LORA_SF          12      // 7..12, higher = longer range, slower
#define LORA_BW          125E3   // 125E3 is a good max-range choice; 62.5E3
                                 // gains ~3 dB but needs good crystal accuracy
#define LORA_CR          8       // coding rate 4/5..4/8 -> 5..8
#define LORA_SYNC_WORD   0x34    // private network sync word, keeps others out
#define LORA_PREAMBLE    8
#define LORA_TX_POWER    20      // dBm, 2..20 on PA_BOOST (Ra-02 uses PA_BOOST)

#define TX_INTERVAL_MS   3000UL  // SF12 packet airtime is ~1.2 s

// ---- Pins ----------------------------------------------------------------
#define PIN_LORA_NSS     10
#define PIN_LORA_RST     8
#define PIN_LORA_DIO0    9       // polled, so no interrupt pin needed
#define PIN_LED          4       // D13 is SPI SCK, so the onboard LED is busy

uint32_t seq = 0;
unsigned long lastTx = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);

  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println(F("LoRa init FAILED - check wiring/power"));
    while (true) {                       // fast blink = radio not found
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      delay(100);
    }
  }

  LoRa.setTxPower(LORA_TX_POWER, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setPreambleLength(LORA_PREAMBLE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();

  Serial.println(F("LoRa range TX ready"));
  Serial.print(F("Freq ")); Serial.print((long)(LORA_FREQ / 1000)); Serial.print(F(" kHz  SF"));
  Serial.print(LORA_SF); Serial.print(F("  BW ")); Serial.print((long)(LORA_BW / 1000));
  Serial.print(F(" kHz  CR4/")); Serial.print(LORA_CR);
  Serial.print(F("  ")); Serial.print(LORA_TX_POWER); Serial.println(F(" dBm"));
}

void loop() {
  if (millis() - lastTx < TX_INTERVAL_MS && seq != 0) return;
  lastTx = millis();
  seq++;

  // Packet: 'R' 'T' seq(4 bytes, little endian) txPower(1 byte) = 7 bytes
  digitalWrite(PIN_LED, HIGH);
  unsigned long t0 = millis();
  LoRa.beginPacket();
  LoRa.write('R');
  LoRa.write('T');
  LoRa.write((uint8_t)(seq));
  LoRa.write((uint8_t)(seq >> 8));
  LoRa.write((uint8_t)(seq >> 16));
  LoRa.write((uint8_t)(seq >> 24));
  LoRa.write((uint8_t)LORA_TX_POWER);
  LoRa.endPacket();                      // blocks until sent
  unsigned long airtime = millis() - t0;
  digitalWrite(PIN_LED, LOW);

  Serial.print(F("TX #")); Serial.print(seq);
  Serial.print(F("  airtime ")); Serial.print(airtime); Serial.println(F(" ms"));
}
