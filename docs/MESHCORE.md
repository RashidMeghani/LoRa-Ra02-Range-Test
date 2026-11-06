# Range test with MeshCore (Heltec WiFi LoRa 32 V2, 433 MHz)

Plan: phone -> **ground node** (Bluetooth) -> **mast node** (~100 m) ->
**far node** (~80 km) and back. Only the ground node needs a phone.

> Written from public MeshCore docs; menu names change between releases.
> Check the current MeshCore docs/flasher before you rely on any step here.
> The custom ping-pong firmware in this repo (`heltecA` / `heltecB`) is the
> cleaner measurement; MeshCore is the "real use" test.

## Roles and firmware

| Node | Where | MeshCore firmware |
|---|---|---|
| Ground | next to the phone | **Companion** (Bluetooth) |
| Mast | ~100 m up, near the ground node | **Repeater** |
| Far | 80 km away, ~100 m up | **Repeater** |

1. Open the MeshCore Flasher (web flasher in Chrome/Edge), select the
   **Heltec V2** and the role above, flash. Make sure your board is listed
   and you picked the **433 MHz** variant if offered.
2. Install the MeshCore app (Android / iOS) or use the web client, connect
   to the ground node over Bluetooth.
3. Set the same radio parameters on all three nodes (frequency 433.xxx MHz,
   bandwidth 125 kHz or 62.5 kHz, spreading factor 11-12, coding rate 4/5-4/8).
   Mismatched settings = silence.
4. Set TX power to your country's legal limit. EU 433 MHz is about 10 dBm.
5. Give the repeaters names (e.g. `MAST`, `FAR`) so they show up in the app.

## Bench test first (before the desert)

1. All three nodes within a few metres, antennas attached, low TX power.
2. In the app, add the mast repeater as a contact (an advert from it appears
   after it announces itself).
3. Use the app's trace/path tool towards `FAR` through `MAST`. You should
   get an SNR for each hop.

## Field test

1. Mast node at ~100 m, powered by a power bank, vertical 433 MHz antenna
   clear of the mast (no metal within ~half a wavelength = 35 cm).
2. Ground node with the phone at the base of the mast. Keep it at low
   power so it does not overload the mast node.
3. Drive the far node out in steps: 1, 5, 20, 40, 60, 80 km. At each stop
   raise it to full height, wait for an advert, then run the trace and write
   down distance, SNR/RSSI for each hop and success/failure in
   `data/range_log_template.csv` (copy it first).
4. Plot with `python3 tools/plot_range.py data/my_test.csv`.

## Notes

- Attribute failures: ground->mast is a short hop, mast->far is the real
  test. A trace showing the first hop but not the second means the 80 km
  link failed, not the phone.
- Pre-compute the expected margin with
  `python3 tools/link_budget.py --km 80 --mhz 433`.
- MeshCore on a phone relies on Bluetooth staying connected; one App Store
  reviewer reported dropouts after 15 minutes to hours. Watch for it.
