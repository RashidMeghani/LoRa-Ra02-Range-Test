# LoRa Ra-02 maximum-range test

A PlatformIO project (VS Code) with two firmwares:

| Environment | Source | Hardware |
|---|---|---|
| `tx` | `src/tx/main.cpp` | Arduino Nano + Ra-02 (+ optional LED on D4) |
| `rx` | `src/rx/main.cpp` | Arduino Nano + Ra-02 + 0.96" SSD1306 I2C OLED (+ optional buzzer on D4) |

Libraries (**LoRa** by Sandeep Mistry, **U8g2**) are fetched automatically
from `platformio.ini`.

### Build and upload (VS Code + PlatformIO)

1. Open this folder in VS Code (PlatformIO extension installed).
2. Pick the environment in the status bar (`env:tx` or `env:rx`), plug in
   that Nano, and click **Upload**. Or from the PlatformIO terminal:

   ```
   pio run -e tx -t upload     # transmitter Nano
   pio run -e rx -t upload     # receiver Nano
   pio device monitor          # 115200 baud
   ```

3. If upload fails with `stk500_getsync(): not in sync`, your Nano has the
   new bootloader: change `board = nanoatmega328` to
   `board = nanoatmega328new` in `platformio.ini`.

Pin map (both boards): NSS D10, RST D8, DIO0 D9, SCK D13, MOSI D11,
MISO D12. OLED (RX): SDA A4, SCL A5.

Radio settings for maximum range (same `#define`s at the top of both
sketches, they must match): 433 MHz, SF12, BW 125 kHz, CR 4/8, CRC on,
private sync word 0x34, +20 dBm on PA_BOOST. The transmitter sends a 7-byte
numbered packet every 3 s (~1.2 s airtime each).

## Schematic (same for both boards, OLED only on RX)

Power: 2S Li-ion/LiPo pack (8.4 V full, ~6.4 V empty).

```
 2S pack  +  ──[fuse 1A]──[switch]──┬──────────────► Nano VIN
 (with BMS)                          │
          -  ────────────────────────┴──────────────► Nano GND ───── common GND
                                                                    (everything)

 Nano 5V ──┬──────────────► AMS1117-3.3 IN
           │                AMS1117-3.3 OUT ──┬── 100uF ──┬── 100nF ──► Ra-02 3.3V
           │                AMS1117-3.3 GND ──┴───────────┴───────────► Ra-02 GND
           └──► OLED VCC   (RX only)

 Nano → Ra-02 signals (Nano is 5 V, Ra-02 is 3.3 V: divide these 4 lines)

   Nano D10 ──[1k]──┬──► Ra-02 NSS         each divider:
   Nano D13 ──[1k]──┬──► Ra-02 SCK            5 V ─[1k]─┬─► 3.3 V pin
   Nano D11 ──[1k]──┬──► Ra-02 MOSI                     [2k]  (or 2k2)
   Nano D8  ──[1k]──┬──► Ra-02 RST                       │
                  [2k] (to GND on each)                 GND

 Ra-02 → Nano (3.3 V into Nano is fine, wire direct)
   Ra-02 MISO ─────────► Nano D12
   Ra-02 DIO0 ─────────► Nano D9

 OLED (RX only, 4-pin I2C module)
   OLED SDA ──► Nano A4      OLED SCL ──► Nano A5
   OLED VCC ──► Nano 5V      OLED GND ──► GND

 Optional
   TX: Nano D4 ──[330R]──►|── GND        (LED, on while transmitting)
   RX: Nano D4 ──► active 5 V buzzer + , buzzer − ──► GND
       (beeps on every packet received)

 Ra-02 ANT ──► 433 MHz antenna (IPEX/u.FL pigtail or soldered wire)
```

### Power notes for the 2S pack

- **Nano VIN** accepts 7–12 V; its on-board AMS1117-5.0 needs ~6.2 V in,
  so a 2S pack runs it right down to empty (6.4 V). Stop the test and
  recharge below ~6.6 V.
- **Do NOT use the Nano's `3V3` pin for the Ra-02.** On most Nano clones it
  comes from the CH340 USB chip and gives only a few tens of mA; the Ra-02
  draws ~120 mA at +20 dBm and will brown out / reset mid-packet.
  Use a separate AMS1117-3.3 module (the cheap blue/red breakout boards)
  fed from the Nano's 5V pin.
- Put the 100 µF + 100 nF capacitors right at the Ra-02's 3.3 V pin; the
  TX current spikes are what usually cause "works on the bench, dies in the
  field" problems.
- Use a pack with a protection BMS (or a 2S BMS board) and a fuse — a 2S
  pack can source tens of amps into a short.
- Optional, more efficient: a small buck converter (MP1584 / Mini-360) set
  to 5.0 V feeding the Nano **5V** pin instead of VIN. Set the voltage
  *before* connecting the Nano.

### Alternative: LF33CV fed straight from the pack (recommended)

```
 BMS P+ ─[fuse]─[switch]─┬──────────────────────► Nano VIN
                         │
                         ├─ 100nF ─ GND
                         └──► LF33CV IN (pin 1)
                              LF33CV GND (pin 2) ─► GND
                              LF33CV OUT (pin 3) ─┬─ 10uF electrolytic/tantalum ─┬─ 100nF ─► Ra-02 3.3V
                                                  └──────────── GND ─────────────┘
```

- LF33CV: 3.3 V LDO, 500 mA, 16 V max in, ~0.45 V dropout — fine on a
  2S pack (6.4–8.4 V).
- **Its output capacitor must be ≥ 2.2 µF with some ESR** (electrolytic or
  tantalum). Ceramic-only output can make it oscillate.
- Heat during TX: (8.4 − 3.3) V × 0.12 A ≈ 0.6 W → about +30 °C on a bare
  TO-220. OK without a heatsink at this duty cycle.
- The Nano's own regulator now only powers the Nano (+ OLED), so it runs
  cooler than when it also fed the Ra-02.
- The LF33CV and Nano VIN both connect to the pack side of the switch
  (BMS P+), which is also where the TP5100 BAT+ lands. Never run the
  boards from the TP5100 with no battery attached — a charger output is
  not a regulated supply.
- When uploading over USB, turn the battery switch on too, otherwise the
  Ra-02 has no 3.3 V.

### Charging the 2S pack with a TP5100 module

The TP5100 is a 1S/2S Li-ion switching charger (up to 2 A). It only
charges; it does **not** balance the cells or protect them, so keep the
2S BMS in the pack.

```
 12 V adapter + ──► TP5100 IN+        TP5100 BAT+ ──► BMS P+ (pack +)
 12 V adapter − ──► TP5100 IN−        TP5100 BAT− ──► BMS P− (pack −)

 BMS P+ ──[fuse]──[switch]──► Nano VIN      (same node as the charger)
 BMS P− ─────────────────────► GND
 BMS B−, BM, B+  ──► cell 1 −, cell mid-point, cell 2 +   (balance wires)
```

- **Input must be 9–18 V for 2S** (12 V adapter is ideal). 5 V USB cannot
  charge a 2S pack.
- **Select 8.4 V.** Most modules have a solder-bridge / jumper for
  4.2 V (1S) vs 8.4 V (2S), and the silkscreen differs between sellers.
  Before connecting the pack, power the TP5100 from 12 V and measure the
  BAT+/BAT− pins with no battery: it must read **~8.4 V**. If it reads
  4.2 V, change the bridge.
- Charge current is set by the on-board sense resistor (typically 1 A or
  2 A). Keep it at or below 1 C of your cells (e.g. 1 A for 1000+ mAh cells).
- Switch the Nano **off** while charging: the TP5100 ends the charge when
  the current falls low, and the device's own ~150 mA draw can stop it
  ever terminating.
- Red LED = charging, green/blue = full. The module gets hot at 2 A;
  give it airflow.

### Dividers

1 kΩ (series) + 2 kΩ (to GND) gives 3.33 V from 5 V. 1k/2k2 (3.4 V) or
10k/20k also work; keep values low (≤ 2k2) on SCK/MOSI for clean edges at
the library's 8 MHz SPI clock. A 4-channel TXS0108E / BSS138 level-shifter
board is an equally good replacement.

## Using it

1. Upload env `tx` to one Nano and env `rx` to the other.
2. Power both. The RX OLED shows `Waiting...` then, once packets arrive:

   ```
    -87 dBm          <- last RSSI (big)
   SNR  9.5 dB
   Margin 29.5 dB    <- SNR above the SF12 decode floor (-20 dB)
   Rx42 Lost1
   Loss 2.3% #43
   Min-104 Tx20dBm   <- weakest RSSI so far, TX power
   Last 1s ago
   ```

3. Leave the TX in a fixed high spot and walk/drive away with the RX. Each
   beep is a received packet. The link edge is where **Margin** approaches
   0 dB, **Loss %** climbs and **Last … ago** keeps rising past 3 s.
4. Plug the RX into a laptop for a CSV log over Serial (115200):
   `ms,seq,rssi,snr,margin,freqErrHz,rx,lost`.
5. Resetting the transmitter restarts the RX statistics automatically.

## Getting the most range

- Use a real **433 MHz** antenna on both ends (quarter-wave ≈ 17 cm wire
  or a tuned whip). The spring coil antennas are several dB worse.
  **Never transmit without an antenna** — it can damage the PA.
- Height matters more than anything: raise both antennas as high as you
  can (clear line of sight + Fresnel zone). Keep antennas vertical and
  parallel, away from metal, batteries and your body.
- Expected: a few hundred metres in town, 2–5 km in open ground with
  hand-held antennas, 10 km+ line of sight with elevated antennas.
- For a few more dB, set `LORA_BW 62.5E3` on **both** sides. Ra-02 crystals
  are ±10 ppm, so check `freqErrHz` in the CSV stays well under ~15 kHz.
- 433 MHz power / duty-cycle limits vary by country (e.g. 10 mW ERP in the
  EU, 1 % duty cycle). Lower `LORA_TX_POWER` / raise `TX_INTERVAL_MS` if your
  local rules require it.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| TX LED fast-blinking / RX shows `LoRa init FAIL` | SPI wiring, dividers, or no 3.3 V at the Ra-02 |
| Works on USB, resets on battery | Ra-02 powered from Nano 3V3, missing capacitors, or pack nearly flat |
| Init OK but nothing received | `#define` settings differ between sketches, or antenna missing |
| OLED blank | Wrong I2C address/controller — try `U8X8_SH1106_128X64_NONAME_HW_I2C` |
| Very weak RSSI even at 1 m | Antenna for wrong band, or on the wrong pad |
