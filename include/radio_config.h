// Radio settings and packet types shared by the mast/far nodes (src/node) and
// the ground node (src/ground). All nodes MUST use identical radio settings.
#pragma once

#define LORA_FREQ        433E6
#define LORA_SF          12
#define LORA_BW          125E3
#define LORA_CR          8
#define LORA_SYNC_WORD   0x34
#define LORA_PREAMBLE    8
#ifndef LORA_TX_POWER
#define LORA_TX_POWER    20      // dBm; lower it to your country's legal limit
#endif

// Ping-pong packet: 'R' '2' type seq(4, LE) rssi(1) snr(1) = 9 bytes
//   rssi byte: -RSSI of the last packet heard from the other node, 0 = none
//   snr  byte: SNR * 4 as int8
#define PKT_LEN          9
#define PKT_PING         1       // mast -> far
#define PKT_PONG         2       // far  -> mast
#define PKT_CMD_PING     3       // ground -> mast: "ping the far node now"

// Report packet, mast -> ground, 16 bytes, sent after every ping cycle:
//   0 'R'  1 '2'  2 type  3..6 seq(LE)
//   7 flags: bit0 far node replied, bit1 far's report of me is valid, bit2 heard far at least once
//   8 -RSSI mast heard far   9 SNR*4 (int8)
//   10 -RSSI far heard mast  11 SNR*4 (int8)
//   12..13 pings sent (LE)   14..15 pings with no reply (LE)
#define PKT_REPORT       4
#define PKT_REPORT_LEN   16
