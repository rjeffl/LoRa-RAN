# LoRa Remote-Automation-Network (LRAN)
This project develops individual elements and data structures to provide bidirectional communication between a Home Assistant (HA) instance and multiple remote automation nodes over a point-to-multipoint LoRa link. 

Planned nodes within this network include:

1. LoRaBridge - Node local to the home WiFi network that bridges commands and data between Home Assistant (MQTT) and remote nodes (LoRa)
2. GateLink - Remote LoRa node the passes operational commands and pulls status to/from a solar powered driveway gate operator system.
3. WellLink - Remote LoRa node that monitors shallow well water level.
