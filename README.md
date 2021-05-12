# BLEcker
**Bluetooth low energy (BLE) tracker for ESP32**

This software is written for ESP32 boards to track BLE devices. It can be used for your smart home, scan BLE devices and send their presence to your smart home hub over MQTT.
This is a ready-to-use program, you don't need to modify the code (add your wifi, mqtt credentials whatever). Settings can be done on a nice web interface.

## What does it exactly
This is a very simple tracker software which creates an MQTT topic for each scanned device mac address (without ":" ) under the base topic and send the availability as payload.

The default topic is **/blecker**\
The default payload for available device: **present**\
The default payload for not available device: **not_present**
### Example

Device MAC is: `12:34:56:ab:cd:ef`
Send an MQTT message if available: 
```
topic: /blecker/123456abcdef
payload: present
```

Send an MQTT message if NOT available: 
```
topic: /blecker/123456abcdef
payload: not_present
```

The scan is running in every 2 seconds for 5 seconds and collects the devices into a list. In case of an available device, it sends an MQTT message as available. If the device is gone or can't be scanned after 120 seconds then a "not available" message will be sent out.

The administrator can define an observable device list in a web frontend. These devices will be uploaded to the inside device list. With this mechanism, the "not available" message can be sent out even if the device was not available right after the reboot.

### Explanation
If the device is not available after the system start, "not available" message will be never sent out just in case the BLE device becomes available and will be gone again.

### Status messages
System sends a detailed status message about the BLE device in every minute: **/blecker/<device-mac>/status**\
The payload is a JSON object structure which contains detailed data like **name**, **rssi**, **observed**, etc. for more possibilities.
This function is off by default. It can be changed on a web administration UI.


## Upload to ESP32
1. Using VSCode and PlatformIO
  * download the source and put it into a folder
  * install VSCode (https://code.visualstudio.com/download) and PlatformIO inside (https://platformio.org/install/ide?install=vscode)
  * open the folder of the source code in VSCode
  * connect your ESP32 to your computer using the USB cable
  * on the bottom of the window there should be a PlatformIO tool: Platform IO upload
  * wait while the code is being built and uploaded

2. Use the ESPtool to upload the prebuild binary
  * download the binary here
  * read how to install and use esptool: https://github.com/espressif/esptool
  * upload the binary: esptool.py --chip esp32 blecker.bin

## First steps

## Web configuration

## Update
