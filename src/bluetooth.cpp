#ifndef BLU
#define BLU

#include "definitions.h"
#include "utilities.cpp"
#include "BluetoothSerial.h" // Header File for Serial Bluetooth
#include "LinkedList.h"
#include "log.cpp"
#include "led.cpp"
#include "database.cpp"
#include <Callback.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>


class BlueTooth: public BLEAdvertisedDeviceCallbacks {

    Log* rlog;
    String log_prefix = "[BLUE] ";
    Led* led;
    Signal<MQTTMessage>* mqttMessageSend;
    BLEScan* pBLEScan;
    Database* database;

    BluetoothSerial blueToothSerial; // Object for Bluetooth
    String command;
    long lastRun = 0;
    long lastClear = 0;
    long lastSendDeviceData = 0;
    int scanAfter = 2000;
    boolean detailedReport = false;

    boolean networkConnected = false; // Connected to the network (Wifi STA)

    LinkedList<Device> devices = LinkedList<Device>();
    LinkedList<int> devicesToRemove = LinkedList<int>();

    public:
        BlueTooth(Log &log, Led &led) {
            this -> rlog = &log;
            this -> led = &led;
        }

        void setup(Database &database, Signal<MQTTMessage> &mqttMessageSend) {

            this -> mqttMessageSend = &mqttMessageSend;
            this -> database = &database;
           
            BLEDevice::init(BOARD_NAME);
            pBLEScan = BLEDevice::getScan(); //create new scan            
            pBLEScan->setAdvertisedDeviceCallbacks(this);
            pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
            pBLEScan->setInterval(100);
            pBLEScan->setWindow(99);  // less or equal setInterval value

            rlog -> log(log_prefix, BOARD_NAME " is initiated");

            // Prefill the device list with the user's devices
            // In case of accidentyy reboot it will send a "not_home" message if the device is gone meanwhile
            // Example:
            // Device is available -> system sends a "home" MQTT message as retained
            // Device is phisically gone (owner go to work) and before the microcontorller sends the "not_home" message microcontroller reboots (power outage)
            // MQTT retained message will be never changed and device stays at home in the MQTT system.
            // Solution:
            // Owner upload the device names which must be observed, so we prefill the list and on the next scan will set them as gone if the story happens above

            // Devices in thsi format: 317234b9d2d0;15172f81accc;d0e003795c50            
            fillDevices(database.getValueAsString(DB_DEVICES));
            // Parse the sting, split by ;

            detailedReport = (database.getValueAsInt(DB_DETAILED_REPORT) > 0) ? true : false;
            
        }

        void loop() {

            if (millis() - lastRun > scanAfter) {
                // Otherwise makes no sens to scan and sent it over
                if (networkConnected) {
                    BLEScanResults foundDevices = pBLEScan->start(5, false);            
                    pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
                    lastRun = millis();
                }
            }

            // Find the expired devices
            for (int i = 0; i < this -> devices.size(); i++) {
                
                Device dev = devices.get(i);
                
                if (millis() - dev.lastSeen > BT_DEVICE_TIMEOUT) {

                    // Give another chance to the device to appear (Device has DEVICE_DROP_OUT_COUNT lives in the beginning)
                    dev.mark--;
                    dev.lastSeen = millis();

                    if (dev.mark == 0) {
                        rlog -> log(log_prefix, (String) "Device is gone. MAC: " + dev.mac);
                        // Send an MQTT message about this device is NOT at home
                        mqttMessageSend->fire(MQTTMessage{dev.mac, getPresentString(false), true});
                        
                        // Virtually remove the device
                        dev.available = false;
                        dev.rssi = "0";
                        devices.set(i, dev);
                    } else {
                        rlog -> log(log_prefix, (String) "Device marked as gone. MAC: " + dev.mac + " Current mark is: " + dev.mark);
                        devices.set(i, dev);
                    }
                } 
            }
            
            if (millis() - lastClear > BT_LIST_REBUILD_INTERVAL && devices.size() > 0) {
                lastClear = millis();
                rlog -> log(log_prefix, (String) "Clear the device list. (This is normal operation. :))");
                // Clear the list, it will be rebuilt again. Resend the (available) status should not be a problem.
                devices.clear();
                mqttMessageSend->fire(MQTTMessage{"selfclean", "true", false});
            }
            
            if (detailedReport) {
                if (millis() - lastSendDeviceData > BT_DEVICE_DATA_INTERVAL && devices.size() > 0) {
                    lastSendDeviceData = millis();
                    rlog -> log(log_prefix, (String) "Send device data.");
                    for (int i = 0; i < this -> devices.size(); i++) {
                        Device dev = devices.get(i);
                        if (dev.mac != NULL && dev.mac.length() > 0) {
                            String payload = "{\"name\":\"" + ((dev.name == NULL) ? "" : dev.name) + "\", \"rssi\":\"" + ((dev.rssi == NULL) ? "" : dev.rssi) + "\", \"mac\":\"" + ((dev.mac == NULL) ? "" : dev.mac) + "\", \"presence\":\"" + getPresentString(dev.available) + "\", \"observed\":\"" + ((dev.observed) ? "true" : "false") + "\"}";
                            mqttMessageSend->fire(MQTTMessage{dev.mac+"/status", payload, false});
                        }
                    }
                }
            }

        }

        void setConnected(boolean connected) {
            this -> networkConnected = connected;
        }
private: 
        // Get the name of the presence string. This is not the state, just the name which will be sent over!!!
        String getPresentString (boolean presenceState) {
            String presence = DEFAULT_NOT_PRESENT;

            if (presenceState) {
                presence = this -> database->getValueAsString(DB_PRECENCE);
                if (presence == "") {                    
                    presence = DEFAULT_PRESENT;
                }
            } else {
                presence = this -> database->getValueAsString(DB_NO_PRECENCE);
                if (presence == "") {                    
                    presence = DEFAULT_NOT_PRESENT;
                }                
            }

            return presence;
        }

        void onResult(BLEAdvertisedDevice advertisedDevice) {
            // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
            //rlog -> log(log_prefix, (String) "Found device MAC: " + advertisedDevice.getAddress().toString().c_str());
            
            boolean newFound = true;
            String deviceMac = advertisedDevice.getAddress().toString().c_str();
            String deviceName = advertisedDevice.getName().c_str();
            String deviceRSSI = (String) advertisedDevice.getRSSI();
            deviceMac.replace(":","");

            for (int i = 0; i < this -> devices.size(); i++) {
                Device dev = devices.get(i);
                if (deviceMac == dev.mac) {

                    // Device came back
                    if (!dev.available) {
                        // Send an MQTT message about this device is at home
                        mqttMessageSend->fire(MQTTMessage{dev.mac, getPresentString(true), true});
                    }
                    dev.lastSeen = millis();
                    dev.mark = DEVICE_DROP_OUT_COUNT;
                    dev.available = true;
                    devices.set(i, dev);                   
                    newFound = false;
                }
            }

            if (newFound) {
                Device dev = {deviceName, deviceRSSI, deviceMac, true, millis(), DEVICE_DROP_OUT_COUNT, false };
                devices.add(dev);
                rlog -> log(log_prefix, (String) "New device found. MAC: " + deviceMac);                
                // Send an MQTT message about this device is at home
                mqttMessageSend->fire(MQTTMessage{dev.mac, getPresentString(true), true});
            }
        }

        void fillDevices(String devicesString) {

            if (devicesString.length() == 0) { return; }

            char *devicesChar = new char[devicesString.length() + 1];
            strcpy(devicesChar, devicesString.c_str());
            String devMac = "";
            while ((devMac = strtok_r(devicesChar, PARSE_CHAR, &devicesChar)) != NULL) { // delimiter is the semicolon
                if (devMac.length() > 0) {
                    Device device = {
                       "", // name
                       "", // rssi
                       devMac, // mac
                       false, // available
                       millis(), // lastSeen
                       DEVICE_DROP_OUT_COUNT, // mark
                       true //observed
                    };
                    this -> devices.add(device);
                    rlog -> log(log_prefix, (String) "Device added as observed device. MAC: " + devMac);
                }
            }

            delete [] devicesChar;
        }

};

#endif