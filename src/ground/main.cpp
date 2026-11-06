// LoRa range test -- GROUND node (Heltec WiFi LoRa 32 V2, 433 MHz)
//
// Your phone joins this board's WiFi hotspot and opens http://192.168.4.1 :
//   phone -> (WiFi) -> GROUND -> (LoRa) -> MAST -> (LoRa) -> FAR -> and back,
//   MAST reports each result to GROUND, GROUND shows it on the phone page.
//
// The mast board runs env heltecA (sends PINGs), the far board runs heltecB
// (answers PONGs). This board listens for the mast's reports and can ask the
// mast to ping right now (the "Ping now" button).
//
// Keep this board's TX power low: it is next to the mast, not 80 km away.

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <U8x8lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "radio_config.h"

// ---- Heltec WiFi LoRa 32 V2 pins ----------------------------------------
#define PIN_LORA_SCK     5
#define PIN_LORA_MISO    19
#define PIN_LORA_MOSI    27
#define PIN_LORA_NSS     18
#define PIN_LORA_RST     14
#define PIN_LORA_DIO0    26
#define PIN_OLED_SDA     4
#define PIN_OLED_SCL     15
#define PIN_OLED_RST     16
#define PIN_VEXT         21      // LOW = power the OLED
#define PIN_LED          25
#define PIN_PRG          0       // PRG button, active LOW: hold 2 s = soft off
#define OFF_HOLD_MS      2000UL

#define AP_SSID          "LoRa-Range"
#define AP_PASS          "loratest1"     // at least 8 characters
#define LINK_LOST_MS     20000UL         // no report for this long = lost
#define HIST_N           15

U8X8_SSD1306_128X64_NONAME_HW_I2C oled(PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA);
WebServer server(80);

const int8_t SNR_FLOOR[] = { -5, -7, -10, -12, -15, -17, -20 };  // SF6..SF12
#define SNR_LIMIT (SNR_FLOOR[LORA_SF - 6])

struct Report {
  uint32_t seq;
  bool ok;            // far node replied to this ping
  bool haveRemote;    // far node's report of how it hears the mast is valid
  bool haveRx;        // mast has heard the far node at least once
  int   l;            // RSSI at which the mast hears the far node
  float ls;           // SNR of that
  int   r;            // RSSI at which the far node hears the mast
  float rs;           // SNR of that
  unsigned long at;   // millis() when received here
};

Report hist[HIST_N];
uint8_t histHead = 0, histCount = 0;
Report last;
bool haveLast = false;
unsigned long reportCount = 0;
uint16_t mastTx = 0, mastLost = 0;
int groundRssi = 0;              // how this board hears the mast
float groundSnr = 0;

volatile bool pendingCmd = false;
uint32_t cmdSeq = 0;
unsigned long cmdSentMs = 0;
unsigned long lastScreenMs = 0, lastOledInitMs = 0, ledOffMs = 0;

// --------------------------------------------------------------------------

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRa range test</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#14171a;--mut:#667;--ok:#1a8f3c;--bad:#c62828;--wait:#b26a00}
@media(prefers-color-scheme:dark){:root{--bg:#101215;--card:#1b1e23;--fg:#eceff3;--mut:#9aa;--ok:#4cc26b;--bad:#ff6b6b;--wait:#ffb04d}}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif}
h1{font-size:20px;margin:0 0 12px}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:12px}
.state{font-size:18px;font-weight:600}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.big{font-size:30px;font-weight:700}
.mut{color:var(--mut);font-size:13px}
button{width:100%;padding:14px;font-size:18px;border:0;border-radius:12px;background:#2d6cdf;color:#fff}
button:disabled{opacity:.5}
table{width:100%;border-collapse:collapse;font-size:14px}
td,th{padding:4px 2px;text-align:right}th:first-child,td:first-child{text-align:left}
</style></head><body>
<h1>LoRa range test</h1>
<div class="card"><div id="state" class="state">Connecting...</div>
<div id="sub" class="mut"></div></div>
<button id="btn" onclick="ping()">Ping now</button>
<div class="grid" style="margin-top:12px">
 <div class="card"><div class="mut">Mast hears far</div><div class="big" id="l">--</div>
  <div class="mut" id="ls"></div></div>
 <div class="card"><div class="mut">Far hears mast</div><div class="big" id="r">--</div>
  <div class="mut" id="rs"></div></div>
</div>
<div class="card"><div id="counts">--</div><div class="mut" id="gm"></div></div>
<div class="card"><table><thead><tr><th>#</th><th>Result</th><th>Mast&larr;Far</th><th>Far&larr;Mast</th></tr></thead>
<tbody id="hist"></tbody></table></div>
<script>
const $=id=>document.getElementById(id);
function setState(t,c){const e=$('state');e.textContent=t;e.style.color='var(--'+c+')';}
function render(j){
 if(!j.have){setState('Waiting for the first report from the mast...','wait');return;}
 if(j.age>20){setState('NO REPORTS - mast off or out of range','bad');}
 else if(!j.ok){setState('No reply from the far node on the last ping','bad');}
 else{setState('Link OK - far node answered','ok');}
 let sub='Last report '+j.age+' s ago, ping #'+j.seq;
 if(j.cmd==='waiting')sub+=' - ping requested, waiting...';
 if(j.cmd==='timeout')sub+=' - ping request not answered';
 $('sub').textContent=sub;
 $('l').textContent=j.l+' dBm';
 $('ls').textContent='SNR '+j.ls.toFixed(1)+'  margin '+j.m.toFixed(1)+' dB';
 $('r').textContent=j.haveRemote?j.r+' dBm':'--';
 $('rs').textContent=j.haveRemote?'SNR '+j.rs.toFixed(1):'';
 const pct=j.tx?(100*j.lost/j.tx).toFixed(1):'0.0';
 $('counts').textContent='Pings '+j.tx+', no reply '+j.lost+' ('+pct+'%)';
 $('gm').textContent='Ground hears mast: '+j.gl+' dBm, SNR '+j.gs.toFixed(1);
 $('hist').innerHTML=j.hist.slice().reverse().map(h=>'<tr><td>'+h[0]+'</td><td>'+(h[1]?'OK':'LOST')
  +'</td><td>'+(h[1]?h[2]+' / '+h[3].toFixed(1):'--')+'</td><td>'+(h[1]&&h[4]!==null?h[4]+' / '+h[5].toFixed(1):'--')+'</td></tr>').join('');
}
async function poll(){try{render(await (await fetch('/status')).json());}
 catch(e){setState('Not connected to the ground node WiFi','bad');}}
async function ping(){const b=$('btn');b.disabled=true;
 try{await fetch('/ping',{method:'POST'});}catch(e){}
 setTimeout(()=>{b.disabled=false;},4000);poll();}
setInterval(poll,2000);poll();
</script></body></html>)HTML";

// --------------------------------------------------------------------------

String statusJson() {
  String j;
  j.reserve(1800);
  j += F("{\"have\":");
  j += haveLast ? F("true") : F("false");
  if (haveLast) {
    unsigned long age = (millis() - last.at) / 1000;
    j += F(",\"age\":");        j += age;
    j += F(",\"seq\":");        j += last.seq;
    j += F(",\"ok\":");         j += last.ok ? F("true") : F("false");
    j += F(",\"haveRemote\":"); j += last.haveRemote ? F("true") : F("false");
    j += F(",\"l\":");          j += last.l;
    j += F(",\"ls\":");         j += String(last.ls, 1);
    j += F(",\"m\":");          j += String(last.ls - SNR_LIMIT, 1);
    j += F(",\"r\":");          j += last.r;
    j += F(",\"rs\":");         j += String(last.rs, 1);
    j += F(",\"tx\":");         j += mastTx;
    j += F(",\"lost\":");       j += mastLost;
    j += F(",\"gl\":");         j += groundRssi;
    j += F(",\"gs\":");         j += String(groundSnr, 1);
    const char *cmd = "idle";
    if (cmdSentMs && last.at < cmdSentMs)
      cmd = (millis() - cmdSentMs < LINK_LOST_MS) ? "waiting" : "timeout";
    j += F(",\"cmd\":\""); j += cmd; j += '"';
    j += F(",\"hist\":[");
    for (uint8_t k = 0; k < histCount; k++) {
      const Report &h = hist[(histHead + HIST_N - histCount + k) % HIST_N];
      if (k) j += ',';
      j += '['; j += h.seq; j += ','; j += h.ok ? '1' : '0';
      j += ','; j += h.l; j += ','; j += String(h.ls, 1);
      j += ',';
      if (h.haveRemote) j += h.r; else j += F("null");
      j += ','; j += String(h.rs, 1); j += ']';
    }
    j += ']';
  }
  j += '}';
  return j;
}

void handleRx() {
  int size = LoRa.parsePacket();
  if (size <= 0) return;
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  uint8_t b[24];
  uint8_t n = 0;
  while (LoRa.available() && n < sizeof(b)) b[n++] = LoRa.read();
  while (LoRa.available()) LoRa.read();
  if (n != PKT_REPORT_LEN || b[0] != 'R' || b[1] != '2' || b[2] != PKT_REPORT) return;

  Report r;
  r.seq = (uint32_t)b[3] | ((uint32_t)b[4] << 8) | ((uint32_t)b[5] << 16) |
          ((uint32_t)b[6] << 24);
  r.ok = b[7] & 1;
  r.haveRemote = b[7] & 2;
  r.haveRx = b[7] & 4;
  r.l  = -(int)b[8];
  r.ls = (int8_t)b[9] / 4.0f;
  r.r  = -(int)b[10];
  r.rs = (int8_t)b[11] / 4.0f;
  mastTx   = b[12] | (b[13] << 8);
  mastLost = b[14] | (b[15] << 8);
  r.at = millis();

  last = r;
  haveLast = true;
  hist[histHead] = r;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
  reportCount++;
  groundRssi = rssi;
  groundSnr = snr;

  digitalWrite(PIN_LED, HIGH);
  ledOffMs = millis() + 40;

  Serial.printf("%lu,%lu,%d,%d,%.1f,%d,%.1f,%u,%u\n", millis(), (unsigned long)r.seq,
                r.ok, r.l, r.ls, r.r, r.rs, mastTx, mastLost);
}

void sendCmd() {
  cmdSeq++;
  LoRa.beginPacket();
  LoRa.write('R');
  LoRa.write('2');
  LoRa.write(PKT_CMD_PING);
  for (uint8_t i = 0; i < 4; i++) LoRa.write((uint8_t)(cmdSeq >> (8 * i)));
  LoRa.write((uint8_t)0);
  LoRa.write((uint8_t)0);
  LoRa.endPacket();                      // blocks ~1.2 s
  cmdSentMs = millis();
  Serial.println(F("sent CMD ping to mast"));
}

void printPadded(uint8_t row, const char *s) {
  char buf[17];
  uint8_t n = strlen(s);
  if (n > 16) n = 16;
  memcpy(buf, s, n);
  memset(buf + n, ' ', 16 - n);
  buf[16] = 0;
  oled.drawString(0, row, buf);
}

void drawScreen() {
  char line[24];
  printPadded(0, "GROUND " AP_SSID);
  printPadded(1, "192.168.4.1");
  if (!haveLast) {
    printPadded(3, "Waiting for");
    printPadded(4, "mast report...");
    return;
  }
  unsigned long age = (millis() - last.at) / 1000;
  snprintf(line, sizeof(line), "#%lu %s", (unsigned long)last.seq,
           age * 1000 > LINK_LOST_MS ? "NO REPORT" : (last.ok ? "OK" : "NO REPLY"));
  printPadded(3, line);
  snprintf(line, sizeof(line), "M<F %4d S%.1f", last.l, last.ls);
  printPadded(4, line);
  if (last.haveRemote) snprintf(line, sizeof(line), "F<M %4d S%.1f", last.r, last.rs);
  else                 snprintf(line, sizeof(line), "F<M  --");
  printPadded(5, line);
  snprintf(line, sizeof(line), "Tx%u Lost%u", mastTx, mastLost);
  printPadded(6, line);
  snprintf(line, sizeof(line), "%lus ago", age);
  printPadded(7, line);
}

// Hold PRG for OFF_HOLD_MS -> WiFi, radio and OLED off, ESP32 in deep sleep.
// Press RST to wake it. Release PRG before pressing RST, otherwise the board
// enters its upload (download) mode.
void softOff() {
  oled.clear();
  oled.drawString(0, 2, "Powering off...");
  oled.drawString(0, 4, "Release PRG.");
  oled.drawString(0, 5, "RST = wake up");
  delay(2500);
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  LoRa.sleep();
  oled.setPowerSave(1);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_VEXT, HIGH);                  // cut power to the OLED
  gpio_hold_en((gpio_num_t)PIN_VEXT);            // keep it cut during sleep
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();                        // no wake source: only RST wakes it
}

// The loop can be busy (sending a command takes ~1.2 s), so an interrupt
// records the moment PRG really went down.
volatile unsigned long prgDownAt = 0;
void IRAM_ATTR onPrgChange() {
  if (digitalRead(PIN_PRG) == LOW) prgDownAt = millis() ? millis() : 1;
  else prgDownAt = 0;
}

void checkOffButton() {
  unsigned long t = prgDownAt;
  if (t != 0 && digitalRead(PIN_PRG) == LOW && millis() - t >= OFF_HOLD_MS) softOff();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PRG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_PRG), onPrgChange, CHANGE);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_VEXT);
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);          // power the OLED
  delay(50);
  oled.setBusClock(400000);
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();
  oled.drawString(0, 0, "LoRa ground node");

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    oled.drawString(0, 2, "LoRa init FAIL");
    Serial.println(F("LoRa init FAILED"));
    while (true) {}
  }
  LoRa.setTxPower(LORA_TX_POWER, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setPreambleLength(LORA_PREAMBLE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE); });
  server.on("/status", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", statusJson());
  });
  server.on("/ping", HTTP_POST, []() {
    pendingCmd = true;
    server.send(200, "text/plain", "ok");
  });
  server.begin();

  Serial.print(F("WiFi AP ")); Serial.print(AP_SSID);
  Serial.print(F("  ")); Serial.println(WiFi.softAPIP());
  Serial.println(F("ms,seq,ok,mastHearsFarRssi,snr,farHearsMastRssi,snr,mastTx,mastLost"));
}

void loop() {
  server.handleClient();
  handleRx();
  checkOffButton();

  if (pendingCmd) {
    pendingCmd = false;
    sendCmd();
  }

  if (ledOffMs && (long)(millis() - ledOffMs) >= 0) {
    digitalWrite(PIN_LED, LOW);
    ledOffMs = 0;
  }

  if (millis() - lastScreenMs >= 500) {
    lastScreenMs = millis();
    if (millis() - lastOledInitMs >= 30000UL) {   // RF can switch the OLED off
      lastOledInitMs = millis();
      oled.initDisplay();
      oled.setFont(u8x8_font_chroma48medium8_r);
      oled.clear();
    }
    oled.setPowerSave(0);
    drawScreen();
  }
}
