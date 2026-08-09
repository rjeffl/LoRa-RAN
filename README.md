# LoRa-Remote-Automation-Network (LRAN)
This project develops elements to provide bidirectional communication between a Home Assistant (HA) instance and multiple
remote automation nodes over a point-to-multipoint LoRa link. 

Planned nodes within this network include:

1. MQTT-LoRa Bridge Node - Node local to the home WiFi network that links HA to remote LoRa nodes through MQTT
2. GateLink Node - Remote LoRa node the allows remote operation and status collection from a solar power gate operator.
3. WellLink Node - Remote LoRa node that monitors shallow well water level.
