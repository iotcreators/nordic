# IoT Creators sample code for Nordic devices

A sample application that connects Nordic devices (e.g. Nordic Thingy:91) to the IoT Creators platform.

## Before you start

Before you compile and install this sample application please follow a basic guide that explains
how to communicate between the IoT Creators platform and your device.

https://docs.iotcreators.com/docs/nordic-thingy91

In the next steps, we assume that your device is already registered on the IoT Creators platform
and you know how to decode payload sent from a device and how to send a downlink message.

## Requirements

 * nRF Connect SDK 1.8.0

## Installation

1. Install [nRF Connect for Desktop](https://www.nordicsemi.com/Software-and-tools/Development-Tools/nRF-Connect-for-desktop/Download#infotabs) on your computer.
2. Follow [nRF Connect SDK Installation Guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_assistant.html) and install version **1.8.0**.
3. Install [VS Code](https://code.visualstudio.com/download).
4. Follow [nRF Connect extensions for VS Code Installation Guide](https://nrfconnect.github.io/vscode-nrf-connect/connect/install.html).
5. Git clone this repository or download it and unzip.
6. Open VS Code.
7. In *NRF CONNECT* extension panel select *Add an existing application* from *Welcome* section.
8. Select cloned or downloaded repository folder.
9. Application (e.g. iotcreators-nordic) should appear in the *Applications* section of *NRF CONNECT* extension panel.
10. Hover the application name (e.g. iotcreators-nordic) in the *Applications* section of *NRF CONNECT* extension panel, 
    an icon named *Add Build Configuration* should appear near the application name. Click it.
11. In the *Add Build Configuration* form leave default values in all fields and click the *Build Configuration* button.
12. Wait several minutes to build.
13. Connect Thingy:91 with a USB cable. Enter boot mode by turning it on while pressing the main button.
14. Open *nRF Connect for Desktop* and open *Programmer* (install it if needed).
15. Use *Add file* and search for **app_signed.hex** in **build\zephyr** folder of your application.
16. Select *Nordic Thingy:91* from the *SELECT DEVICE* dropdown.
17. After clicking *Write* you will see the *MCUboot DFU* popup.
18. Hit the *Write* button in the *MCUboot DFU* popup.
19. Wait 20-40 seconds until success message is displayed.
20. Power cycle the device to start the application.

## LED indication

| Status                | LED                  |
| ----------------------|----------------------|
| LTE connection search | Light blue, blinking |
| Active mode           | Green, blinking      |

## Usage

In *Active mode* the application every 60 seconds sends to IoT Creators platform basic sensors readings.

```JSON
{"Temp":21.98,"Press":1.0116,"Humid":39.99,"Gas":13086,"Vbat":4371}
```

Immediately after pressing the main button, the application sends this information to the IoT Creators platform.

```JSON
{"Msg":"Event: Thingy:91 button pressed, 000000000000000"}
```

where 000000000000000 is the device IMEI number.
