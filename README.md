# RoseController
Remotely controllable rose controller for Beauty and the Beast performance; based on NodeMCU.

The controller allows control over 3 separate lights (Spot, Dome, Pixie) and over 5 separate servos
that drop one or more petals each by pulling on a fishing line with a small magnet at the end of it.
This pulls away the magnet that keeps the petal attached, causing the petal to drop.

The controller exposes its own WiFi network and a web-server that can be used to access a small
web-based application allowing remote control from a computer or a mobile device.

This project is also accompanied by [RoseTransmitter project](https://github.com/tomikazi/RoseTransmitter)
that serves a harware remote control for the rose in lieu of a mobile device.

The software is built atop the [ESP32Gizmo platform library](https://github.com/tomikazi/ESP32Gizmo).
