# GateLink MPPT configuration

**The MPPT 75/15's charger settings, as configured and as read back.** GateLink PRD
R-6.1b asks for this record. The operator set the charger for LiFePO4 in VictronConnect.
GateLink's HEX path and the bridge's BF-30 readback confirmed the settings on 2026-10-10.

The controller is an MPPT 75/15, PID `0xA075`, firmware 175. It charges the
WattCycle 100 Ah LiFePO4 pack (PRD R-6.1a).

## The settings

Each value appears three times: in VictronConnect's Battery settings page, as the raw HEX
answer GateLink logged, and as BF-30's decoded value on
`lran/gatelink/vedirect/charge/state`. All three agree.

| Setting | VictronConnect | Register | Raw | BF-30 |
|---|---|---|---|---|
| Battery voltage | 12V | `0xEDEA` | `0x0C` | 12 V |
| Max charge current | 15A | `0xEDF0` | `0x0096` | 15.0 A |
| Battery preset | Smart Lithium (LiFePo4) | `0xEDF1` | `0xFF` | 255 |
| Absorption voltage | 14.20V | `0xEDF7` | `0x058C` | 14.20 V |
| Float voltage | 13.50V | `0xEDF6` | `0x0546` | 13.50 V |
| Equalization voltage | Disabled | `0xEDF4` | `0x0000` | 0.00 V |
| Automatic equalization | Disabled | `0xEDFD` | `0x00` | 0 |
| Temperature compensation | Disabled | `0xEDF2` | `0x0000` | 0.00 mV/K |
| Low temperature cut-off | 5°C | `0xEDE0` | `0x01F4` | 5.00 °C |
| Absorption time limit | not shown | `0xEDFB` | `0x00C8` | 2.00 h |

VictronConnect also shows *Charger enabled* on and *Expert mode* off. The app hides the
absorption time limit while expert mode is off, so that row has only the readback.

**The battery type register reads `0xFF`, user-defined, while the app names a preset.**
The app shows *Smart Lithium (LiFePo4)*, but the register holds no preset number. R-6.1b
asks for a user-defined battery type, and `0xFF` is that.

## Against R-6.1b and the pack

| R-6.1b asks for | Configured | Verdict |
|---|---|---|
| Battery type user-defined | `0xEDF1` = `0xFF` | Met |
| Equalization disabled | Automatic equalization off; equalization voltage disabled | Met |
| Temperature compensation 0 mV/°C | Disabled, reads 0 | Met |
| Absorption per the pack specification | 14.20 V | Inside 14.2–14.6 V, outside 14.6 ± 0.2 V; see below |
| Float per the pack specification | 13.50 V | Met: the pack gives 13.4–13.6 V |

**The pack's absorption figure contradicts itself.** The operator supplied the WattCycle
100Ah Mini's charging specification on 2026-10-10. It gives the recommended charge
(absorption) voltage as "14.2V – 14.6V (14.6V ± 0.2V)". 14.20 V falls inside the first
range and below the second, which spans 14.4–14.8 V. This record does not settle which
the vendor means.

The same specification recommends 20 A (0.2C) of charge current. The 75/15 is rated
for 15 A, so the controller's 15 A setting is its own limit.

## Trace

[`data/mppt-readback-2026-10-10.log`](data/mppt-readback-2026-10-10.log) holds the
bridge's boot readback: ten HEX Gets, each answered on its first attempt. The engineering
log's 2026-10-10 entry has the context.
