#!/usr/bin/env python3
"""Link budget / line-of-sight check for a long LoRa link.

Example (the 80 km, 433 MHz, 100 m masts test):
    python3 tools/link_budget.py --km 80 --mhz 433 --h1 100 --h2 100 \
        --tx-dbm 20 --gain1 3 --gain2 3 --cable-loss 1 --sf 12 --bw 125
"""
import argparse
import math

# SX127x sensitivity at BW125 (dBm), approximate datasheet values.
SENS_BW125 = {7: -123, 8: -126, 9: -129, 10: -132, 11: -134.5, 12: -137}


def main():
    a = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("--km", type=float, required=True, help="link distance, km")
    a.add_argument("--mhz", type=float, default=433.0)
    a.add_argument("--h1", type=float, default=100.0, help="antenna height end 1, m")
    a.add_argument("--h2", type=float, default=100.0, help="antenna height end 2, m")
    a.add_argument("--tx-dbm", type=float, default=20.0)
    a.add_argument("--gain1", type=float, default=3.0, help="antenna gain end 1, dBi")
    a.add_argument("--gain2", type=float, default=3.0, help="antenna gain end 2, dBi")
    a.add_argument("--cable-loss", type=float, default=1.0, help="total cable+connector loss, dB")
    a.add_argument("--sf", type=int, default=12, choices=sorted(SENS_BW125))
    a.add_argument("--bw", type=float, default=125.0, help="bandwidth, kHz")
    a.add_argument("--k", type=float, default=4 / 3, help="effective earth radius factor")
    a.add_argument("--fade", type=float, default=10.0, help="fade margin to keep, dB")
    a = a.parse_args()

    d = a.km
    fspl = 20 * math.log10(d) + 20 * math.log10(a.mhz) + 32.44
    rx = a.tx_dbm + a.gain1 + a.gain2 - a.cable_loss - fspl
    sens = SENS_BW125[a.sf] + 10 * math.log10(a.bw / 125.0)
    margin = rx - sens

    # Geometry
    horizon = 4.12 * (math.sqrt(a.h1) + math.sqrt(a.h2)) * math.sqrt(a.k / (4 / 3))
    # Mid-path (worst case for a flat path): line of sight height vs earth bulge
    d1 = d2 = d / 2
    bulge = d1 * d2 / (12.74 * a.k)
    los = (a.h1 + a.h2) / 2
    clearance = los - bulge
    f1 = 17.3 * math.sqrt(d1 * d2 / ((a.mhz / 1000) * d))
    print(f"Distance            : {d:.1f} km @ {a.mhz:.0f} MHz")
    print(f"Free-space loss     : {fspl:.1f} dB")
    print(f"Received power      : {rx:.1f} dBm (before diffraction/terrain)")
    print(f"Sensitivity SF{a.sf}/{a.bw:.0f}k: {sens:.1f} dBm")
    print(f"Margin              : {margin:.1f} dB  (keep >= {a.fade:.0f} dB for fading)")
    print(f"Radio horizon       : {horizon:.1f} km for h1={a.h1:.0f} m, h2={a.h2:.0f} m")
    print(f"Earth bulge @ mid   : {bulge:.1f} m   LOS height there: {los:.1f} m")
    print(f"Clearance @ mid     : {clearance:.1f} m   (1st Fresnel radius {f1:.1f} m, "
          f"60% = {0.6 * f1:.1f} m)")
    if d > horizon:
        print("!! Beyond the radio horizon: expect heavy diffraction loss.")
    elif clearance < 0.6 * f1:
        print("!  Less than 60% Fresnel clearance: expect extra loss; "
              "raise a mast or choose higher ground.")
    if margin < a.fade:
        print("!  Margin below the fade target: more antenna gain, higher masts, "
              "or a slower data rate.")


if __name__ == "__main__":
    main()
