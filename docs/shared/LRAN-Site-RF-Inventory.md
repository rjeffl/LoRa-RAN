# LRAN site RF inventory — the 900 MHz equipment on the property

**Document:** `LRAN-Site-RF-Inventory`
**Version:** 0.1
**Status:** **Reference — desk research, not measurement.** Every frequency, rate and power
figure below comes from a published datasheet, developer guide, product page or FCC grant,
cited in *Sources*. Nothing here was measured from the property's own devices, and no
device's model number has been checked against this list.
**Parent document:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **§3.1**
(the site inventory D33 was reasoned against) and **M26** (the measurement item this answers
in part)
**Last updated:** 2026-09-18

> **Three things this research establishes.** None of the property's Z-Wave, Insteon or
> YoLink equipment has a nominal channel within 1.4 MHz of LRAN's 917.4 MHz. **The Davis
> weather station does**: one of its 51 hop channels is 917.434 MHz, and its hop cycle
> predicts M25's periodic source to the millisecond. And M20's loudest signal at the bridge,
> −54 dBm at 916.0 MHz, sits on Z-Wave's 100 kbps channel rather than on YoLink's.
>
> **The Davis is identified by prediction, not by decoding.** The hop cycle predicted a
> period of 130.6875 s at transmitter ID 1 before anyone read the console. The operator then
> read ID 1 on 2026-09-18, and M25 had measured 130.69 s. No Davis packet has been decoded on
> 917.4 MHz.

## 1. What is on the property

The operator identified the equipment on 2026-09-17 and 2026-09-18. Decision Register §3.1
recorded YoLink only until then.

| System | What is deployed | Radio |
|---|---|---|
| **Z-Wave** | Standard Z-Wave mesh. A **500-series controller**; no device newer than the **700 series** | Classic Z-Wave, sub-GHz FSK/GFSK |
| **Insteon** | A **2413U PowerLinc Modem (USB)**, plus a mix of **dual-band** and **wireless-only** devices. No Insteon Hub | Insteon i2/RF |
| **YoLink** | A hub, four temperature sensors and a switch, all inside the dwelling (§3.1) | LoRa, proprietary protocol |
| **Davis** | A **Davis Vantage Pro2** weather station, **transmitter ID 1**. Its transmitter sits at M20's survey position 2, `weather-island`, **on the line between the bridge and GateLink's site** | Frequency-hopping GFSK |
| **Dakota Alert** | A driveway occupancy sensor, standing in for the gate controller, which LRAN cannot reach yet. The transmitter is within 30 ft of the gate controller, at M20's survey position 1, `gatelink-gate`; the receiver is near the production bridge location. Model not yet recorded | 433.92 MHz, outside the 902–928 MHz band (§7) |

**Insteon's "dual-band" means powerline plus RF, not two radio frequencies.** A dual-band
device carries Insteon's 131.65 kHz powerline signal and the same 915 MHz radio that a
wireless-only device uses. For this inventory, both kinds are one radio.

## 2. Summary

| System | Centre frequency | Modulation and rate | Deviation and bandwidth | Transmit power | FCC basis | Offset from 917.4 MHz |
|---|---|---|---|---|---|---|
| **Z-Wave, 100 kbps** | **916.00 MHz** | GFSK, NRZ, 100 kbps | ±29.3 kHz; 110 kHz channel bandwidth | 500 series: +5.9 dBm maximum conducted. 700 series: +4 dBm maximum setting in classic mode | §15.249 (500-series datasheet) | **−1.40 MHz** |
| Z-Wave, 40 kbps | 908.40 MHz | FSK, NRZ, 40 kbps | ±20 kHz; 90 kHz | as above | §15.249 | −9.0 MHz |
| Z-Wave, 9.6 kbps | 908.42 MHz | FSK, Manchester, 9.6 kbps | ±20 kHz; 90 kHz | as above | §15.249 | −9.0 MHz |
| *Z-Wave Long Range* | *912 and 920 MHz* | *DSSS O-QPSK, 100 kbps* | — | *+14 dBm in the 700-series datasheet* | — | ***Not in use*** — see §3 |
| **Insteon i2/RF** | **915.000 MHz**; grants 914.9–915.1 MHz, the PLM 914.94 MHz | FSK, Manchester; 9,124 symbols/s, 4,562 bit/s | **200 kHz peak-to-peak**, so tones near 914.90 and 915.10 MHz | Not stated in any source found | "Part 15 Low Power Transceiver", subpart C | −2.4 MHz |
| **YoLink** | **923.3 MHz** | LoRa; SF and bandwidth not published | Not published | Not stated | "Part 15 Low Power Communication Device Transmitter", subpart C | +5.9 MHz |
| **Davis** | **51 hop channels, 902.382–927.470 MHz**, about 0.5 MHz apart; one at **917.434 MHz** | GFSK, 19.2 kbps; 16 bytes on air, **6.7 ms** a packet | See §6 | Not stated | Frequency hopping, per Davis | **+0.034 MHz on one channel**, inside the 125 kHz receive bandwidth |
| *Dakota Alert* | *433.92 MHz* | — | — | Not stated | §15.231, periodic and security transmitters | *−483.5 MHz; outside the band* |

**The §15.249 ceiling is the same one LRAN works under.** It limits the fundamental to
50 mV/m at 3 m, about **−1.2 dBm EIRP**, as Protocol Specification §18.2 states for LRAN. A
chip's maximum conducted output is not what a certified product transmits; the product is set
below the ceiling. The Z-Wave power figures above are chip maxima for that reason.

**Two rule sections are inferred, not read from a test report.** The Insteon and YoLink
grants list rule part "15C" and an equipment class without naming a section, and neither
grant lists an output power. The same YoLink hub's Wi-Fi radio is certified as a "Digital
Transmission System" with an output power in watts, which is how a §15.247 grant reads. So
the 900 MHz grants are consistent with §15.249. The Insteon test reports that would settle it
could not be retrieved (fccid.io and fcc.report both refused them).

## 3. Z-Wave

**Classic Z-Wave in the US uses two frequencies and three rates.** The 500-series ZM5101
datasheet (Table 6.1) gives 908.42 MHz for 9.6 kbps, 908.40 MHz for 40 kbps and 916.00 MHz
for 100 kbps. The 700-series ZGM130S datasheet quotes the same frequencies for its classic
modes. Both parts share one modulation table:

| Rate | Modulation | Deviation | Coding | Channel bandwidth (ZM5101) |
|---|---|---|---|---|
| 9.6 kbps | FSK | ±20 kHz | Manchester | 90 kHz |
| 40 kbps | FSK | ±20 kHz | NRZ | 90 kHz |
| 100 kbps | GFSK | ±29.3 kHz | NRZ | 110 kHz |

**Power.** The ZM5101's output is set by register, from −24 dBm to a maximum of +5.9 dBm
conducted (Table 5.20). Its datasheet lists FCC compliance under Part 15 Subpart C §15.249.
The ZGM130S's classic-mode maximum setting is +4 dBm. Its datasheet gives spurious and
power-spectral-density figures against §15.249, and tells the integrator to set output power
to the region's limits.

**Z-Wave Long Range is not in use here.** Long Range needs a controller that supports it, and
a 500-series controller does not. The 700 series added Long Range, at 912 and 920 MHz with
DSSS O-QPSK, and the ZGM130S datasheet specifies it at up to +14 dBm. **If the controller is
ever replaced with a Long Range one, this inventory changes.** 920 MHz is 2.6 MHz from
917.4 MHz, and at that power it would be the strongest neighbour on the property.

**Which rate carries which traffic is not stated in the sources used here.** Every 500- and
700-series device supports 100 kbps, so traffic between two such devices can use 916.00 MHz.
M20 saw energy on both 916.0 and 908.4 MHz (§8).

## 4. Insteon

**Insteon's radio is i2/RF, at 915 MHz.** SmartLabs' *INSTEON Developer's Guide* (2007,
chapter 6) specifies it, and every Insteon grant sampled here, from 2009 to 2012, sits within
100 kHz of 915 MHz:

| i2/RF | Value |
|---|---|
| Centre frequency | 915.0000 MHz |
| Modulation | FSK |
| FSK deviation | 200 kHz peak-to-peak |
| Data encoding | Manchester |
| Symbol rate | 9,124 symbols per second |
| Data rate | 4,562 bits per second |
| Range | 400 ft line of sight, half-wave dipole, 0.1 % raw bit-error rate |

**The deviation is wide.** A zero symbol sits 100 kHz below the centre and a one symbol
100 kHz above it, so the signal occupies roughly 914.90–915.10 MHz. The FCC grants agree:
914.9–915.1 MHz for the 2487S dual-band switch and the Insteon Hub, 915.0 MHz for the 2440
RemoteLinc, and **914.94 MHz for the 2413U PLM**. The `insteonrf` SDR project receives
current devices at 915 MHz and notes that real devices sit tens of kHz off nominal.

**The earlier i1/RF radio is not expected here.** It ran at 904 MHz with 64 kHz deviation at
38,400 bit/s, and the guide names one product that used it: the SignaLinc RF Signal Enhancer
of May 2005. Some secondary sources quote i1/RF's 38.4 kbps rate for Insteon RF generally;
that figure does not describe current devices.

**No source found gives Insteon's transmit power.** The grants list none, and the test reports
could not be retrieved.

## 5. YoLink

**YoLink uses 923.3 MHz.** YoLink's product pages give "LoRa: 923.3MHz" for the hub and its
devices. The hub's FCC grant (2ATM71603, 2019) lists 923.3–923.3 MHz as a "Part 15 Low Power
Communication Device Transmitter". LoRa spreading factor, bandwidth and output power are not
published.

## 6. Davis

**A Davis Vantage Pro2 station hops across 51 channels and returns to each one every 130.69 s.** The
DavisRFM69 project, which receives Davis stations on an RFM69 radio, carries the North
American hop table: 51 channels from 902.382 to 927.470 MHz, about 0.5 MHz apart, visited in
a fixed order. Its protocol notes give the transmit interval as 2.5 s plus 1/16 s per
transmitter ID step, so 2.5625 s at ID 1. **51 × 2.5625 s = 130.6875 s.**

**One of the 51 channels is 917.434 MHz**, 34 kHz above LRAN's centre. That falls inside the
SX1262's 125 kHz receive bandwidth, so a Davis packet lands in LRAN's channel once per hop
cycle.

**A packet is 16 bytes on air**: four preamble bytes, two sync bytes and a 10-byte payload,
at 19.2 kbps. That is **6.7 ms**. DavisRFM69 transmits with 9.9 kHz deviation when it emulates
a station; Davis does not publish the figure.

**This matches M25's periodic source.** The capture measured a period of **130.69 s**, a burst
of about 7 ms estimated from the catch rate, and a level near −75 dBm at the bridge. Bridge
engineering log, 2026-09-18.

**The transmitter ID confirmed it.** ID 1 predicted 130.6875 s, and any other ID would have
predicted a different period and ruled the Davis out. The operator read ID 1 on the console on
2026-09-18 and noted that it is probably the factory default. M25's fitted period, 130.6865 s,
is 1 ms per cycle short of the prediction, about 8 parts per million, which is within what a
transmitter's crystal would explain.

**Every additional Davis transmitter would be a second periodic source.** An anemometer
transmitter or a leaf-and-soil station at another ID would hop the same table on its own cycle.
The M25 capture shows one periodic source at 917.4 MHz, which is consistent with a single
transmitter.

## 7. Dakota Alert — checked, and outside the band

**Every Dakota Alert Part 15 product found transmits on 433.92 MHz**, so the driveway sensor
is not one of LRAN's neighbours. The operator expected 433 MHz, and the FCC grants agree. Among
Dakota Alert's 19 FCC IDs:

| FCC ID | Product | Frequency | Rule part | Granted |
|---|---|---|---|---|
| [QK83000T](https://fccid.io/QK83000T) | Driveway Radio | 433.92 MHz | 15C | 2004-12-21 |
| [QK8UT3000](https://fccid.io/QK8UT3000) | Universal Transmitter (3000 series) | 433.92 MHz | 15.231 | 2006-05-12 |
| [QK8DCT-2500](https://fccid.io/QK8DCT-2500) | DCT-2500 transmitter module | 433.92 MHz | 15.231 | 2010-02-09 |
| [QK8DCT-4000](https://fccid.io/QK8DCT-4000) | DCT-4000 wireless transmitter | 433.92 MHz | 15.231 | 2018-10-12 |
| [QK8PB-4000-B](https://fccid.io/QK8PB-4000-B) | PB-4000 wireless probe transmitter | 433.92 MHz | 15C | 2024-01-25 |
| [QK8RH-4000](https://fccid.io/QK8RH-4000) | DCHT-4000 rubber hose transmitter | 433.92 MHz | 15C | 2024-12-03 |
| [QK8MALERT](https://fccid.io/QK8MALERT) | MURS Alert | **151.8–154.6 MHz**, 1.1 W ERP | 95J (MURS) | 2002-10-25 |

**No Dakota Alert grant opened here is in 902–928 MHz.** The other 12 IDs are nine 2500- and
4000-series transmitters and sensors, which were not opened individually, and three more MURS
models. Retailers describe both the 2500 and 4000 series as 433 MHz.

**No harmonic lands near LRAN's channel.** The second harmonic of 433.92 MHz is 867.84 MHz,
below the band. **The MURS line is the one caveat, and only if the sensor is a MURS model.** The
sixth harmonics of the five MURS channels fall at 910.9–911.6 MHz and 927.4–927.6 MHz: more
than 5 MHz from 917.4 MHz, but inside the band's top edge.

**The model number settles which family this is.** A 2500-, 3000- or 4000-series sensor is
433.92 MHz; a MURS sensor is VHF.

## 8. What M20's survey saw at each frequency

**M20 measured 125 kHz every 200 kHz, with 653 samples per bin**, so each figure below is a
peak over a short dwell. The table gives peak dBm at each site; the floor was −115 to −117 dBm
everywhere. Source: `docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`.

| Bin (MHz) | What sits there | bridge-house | gatelink-gate | weather-island | welllink-well | propane-tank |
|---|---|---|---|---|---|---|
| 908.4 | Z-Wave 9.6 and 40 kbps; a Davis channel (908.403) | −113 | −110 | −97 | −113 | **−88** |
| 914.8 | Insteon's lower tone falls in the gap above this bin | −112 | −112 | −97 | −112 | −113 |
| 915.0 | Insteon centre; both tones fall in the gaps either side | −113 | −113 | **−80** | −111 | −114 |
| 915.2 | Insteon's upper tone falls in the gap below this bin | −113 | −113 | −95 | −113 | −106 |
| 916.0 | **Z-Wave 100 kbps** | **−54** | −89 | −113 | −113 | −112 |
| 917.4 | LRAN; Davis 917.434 | −113 | −113 | −110 | −111 | −113 |
| 923.2 / 923.4 | YoLink (923.3) straddles the gap between | −113 / −113 | −110 / −104 | −113 / −104 | −113 / −111 | −113 / −113 |

**The −54 dBm at 916.0 MHz inside the house matches Z-Wave, not YoLink.** Decision Register
§5.4 named the YoLink hub as the obvious candidate for it and marked that unconfirmed. YoLink
transmits at 923.3 MHz, where both neighbouring bins read −113 dBm at the same spot. Z-Wave's
100 kbps channel is 916.00 MHz. Where the Z-Wave controller and its mains-powered devices sit
relative to the bridge has not been recorded. **§5.4's attribution looks wrong**, and the register is where that gets corrected. It does not change D1: the
recommendation was to avoid the cluster, whatever produces it.

**The survey was nearly blind to a centred Insteon device.** With tones at 914.90 and
915.10 MHz, a device exactly on 915.000 MHz puts both tones in the 75 kHz gaps between bins.
A device a few tens of kHz off, as `insteonrf` reports real devices are, would move one tone
into a bin. So a floor reading near 915.0 MHz is weak evidence that Insteon is quiet there.

**The survey cannot confirm or rule out the Davis, even at its own site.** The Davis
transmitter sits at `weather-island`. No site's bins that contain a Davis channel read higher
than its other bins, `weather-island`'s included; the chance that a Davis bin outranks a non-Davis bin
runs from 0.42 to 0.55 across the seven sites. A 6.7 ms burst that returns to one channel every
130.7 s would rarely fall inside one bin's dwell. M25 caught it by watching one channel for ten
hours.

**Two peaks remain unattributed.** At weather-island, −80 dBm at 915.0 MHz is Insteon, the
Davis or something else. M20 recorded it as local to that site, and the Davis transmitter is
there, but its nearest hop channel, 914.927 MHz, sits at the edge of the 915.0 MHz bin rather
than inside it. At propane-tank,
−88 dBm at 908.4 MHz is Z-Wave, the Davis channel at 908.403 MHz or something else.

## 9. What it means for LRAN on 917.4 MHz

**The Davis is the only inventoried transmitter that lands in LRAN's channel.** Once every
130.69 s, for about 6.7 ms. **It sits on the path between the bridge and the gate**, so both
ends of the link hear it: the bridge measured it near −75 dBm, and GateLink's receiver will
hear it at a level nobody has measured. At the bench, where the wanted signal is −37 dBm, a −75 dBm burst
does not matter. At the gate, where it is about −100 dBm, a Davis burst that overlaps an uplink
frame is likely to lose it. The bridge engineering log's 2026-09-18 entry estimates that as
about 0.85 % of maximum-length SF9 frames, from timing alone.

**The overlap is a property of 917.4 MHz, and a small move would remove it.** The Davis
channels either side of 917.434 MHz are 916.934 and 917.936 MHz, so a 125 kHz channel centred
between them, 917.2 MHz for example, contains no Davis hop. A move is a D1 matter and is not
proposed here; the Davis is one source among several, and M25's second source is unexplained.

**Z-Wave's 916.00 MHz is the loudest neighbour, 1.4 MHz away.** Whether its energy reaches the
bridge's 917.4 MHz RSSI reading or its demodulator depends on the SX1262's selectivity at that
offset. This document does not establish that.

**M25's second source, near −93 dBm and episodic, is not explained by anything here.** None of
the inventoried systems puts a nominal channel at 917.4 MHz except the Davis, and the Davis does
not transmit in episodes lasting seconds.

## 10. Open, and how each item closes

| Item | Closed by | Owner |
|---|---|---|
| ~~Davis transmitter ID~~ | **Closed 2026-09-18: ID 1**, read on the console | Operator |
| Dakota Alert model number, to confirm 433.92 MHz rather than MURS | The transmitter's or receiver's label | Operator |
| The Davis burst's level at GateLink's site | A capture at the gate, or GateLink's own RSSI once it runs | GateLink bring-up |
| Z-Wave and Insteon device model numbers against §3 and §4 | The controller's and the PLM's device lists | Operator, **M26** |
| Whether the 916.0 MHz cluster is Z-Wave | The Z-Wave controller's frame log, time-aligned with a survey dwelling on 916.0 MHz | **M26** |
| M25's −93 dBm episodic source | Not yet known | Open |
| Decision Register §3.1 inventory and §5.4's YoLink attribution | A register revision | Operator, **D33** |
| Insteon transmit power and rule section | An FCC test report, retrieved from a source that allows it | Open, low priority |

## Sources

- Silicon Labs, [ZM5101 datasheet DSH12625-9](https://www.silabs.com/documents/public/data-sheets/DSH12625-9.pdf), 2018 — Z-Wave 500 series: Table 5.20 transmitter, Table 6.1 frequencies, §15.249 compliance.
- Silicon Labs, [ZGM130S datasheet](https://www.silabs.com/documents/public/data-sheets/zgm130s-datasheet.pdf), rev. 1.3 — Z-Wave 700 series: Table 4.9 classic-mode transmitter, Table 4.10 Long Range at +14 dBm.
- Silicon Labs, [EFR32ZG14 datasheet](https://www.silabs.com/documents/public/data-sheets/efr32zg14-datasheet.pdf) — Z-Wave 700 modem SoC, for the +14 dBm Long Range figure.
- Home Assistant Community, [Z-Wave 500/700/800 compatibility thread](https://community.home-assistant.io/t/z-wave-500-700-800-no-benefit-if-i-add-800-device-on-500-device/945719) and Versa Wireless, [Z-Wave 800 vs 700](https://versawireless.com/blogs/insight/zwave-800-vs-700-series-installers-guide) — Long Range needs a Long Range controller; 500-series controllers do not support it.
- SmartLabs, [INSTEON Developer's Guide](https://cache.insteon.com/pdf/INSTEON_Developers_Guide_20070816a.pdf), 2007-08-16, chapter 6 — i2/RF and i1/RF physical layers.
- [apnar/insteonrf](https://github.com/apnar/insteonrf) — Insteon RF received at 915 MHz; devices sit tens of kHz off nominal.
- FCC grants via fccid.io: [SBP2413U](https://fccid.io/SBP2413U) (PLM, 914.94 MHz), [SBP2487S](https://fccid.io/SBP2487S) (dual-band switch, 914.9–915.1 MHz), [SBP2440](https://fccid.io/SBP2440) (RemoteLinc, 915.0 MHz), [SBP22422](https://fccid.io/SBP22422) (Insteon Hub, 914.9–915.1 MHz; not deployed here), [2ATM71603](https://fccid.io/2ATM71603) (YoLink hub, 923.3 MHz).
- YoSmart, [YoLink Hub product page](https://shop.yosmart.com/products/ys1603) — "LoRa: 923.3MHZ".
- [dekay/DavisRFM69](https://github.com/dekay/DavisRFM69) — `DavisRFM69.h` North American hop table and packet length, `DavisRFM69.cpp` preamble, sync and bit rate; and its [RF Protocol wiki page](https://github.com/dekay/DavisRFM69/wiki/RF-Protocol) — transmit interval by ID.
- FCC grants for Dakota Alert (grantee [QK8](https://fccid.io/QK8)): QK83000T, QK8UT3000, QK8DCT-2500, QK8DCT-4000, QK8PB-4000-B, QK8RH-4000 and QK8MALERT, linked in §7. [Absolute Automation, Dakota Alert 4000](https://www.absoluteautomation.com/collections/dakota-alert-4000) — 4000 series at 433 MHz.
- Davis Instruments, [Vantage Pro2 ISS](https://www.davisinstruments.com/products/wireless-vantage-pro2-integrated-sensor-suite) — frequency-hopping spread-spectrum radio.
- LRAN: [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) §3.1, §3.4, §5.4, M20, M25, M26; [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) §12.1, §18.2; [bridge engineering log](../bridge/engineering-log.md), 2026-09-18; [`docs/rangetest/data/README.md`](../rangetest/data/README.md), survey schema and coverage.

## Changelog

| Version | Date | Change |
|---|---|---|
| **v0.1** | 2026-09-18 | Initial release. The Davis transmitter ID was read as 1 the same day, confirming the hop-cycle prediction, before the document merged. The Dakota Alert driveway sensor was added the same day, as checked and outside the band. Z-Wave, Insteon, YoLink and Davis specifications from published sources; M20's survey read at each frequency; the Davis hop cycle matched to M25's periodic source |
