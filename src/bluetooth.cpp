#ifndef BLU
#define BLU

#include "definitions.h"
#include "utilities.cpp"
#include "BluetoothSerial.h" // Header File for Serial Bluetooth
#include "LinkedList.h"
#include "log.hpp"
#include "led.cpp"
#include "database.cpp"
#include <Callback.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>


class BlueTooth: public BLEAdvertisedDeviceCallbacks {

    Logger logger;
    Led* led;
    Signal<MQTTMessage>* mqttMessageSend;
    Signal<Device>* deviceChanged;
    BLEScan* pBLEScan;
    Database* database;

    BluetoothSerial blueToothSerial; // Object for Bluetooth
    String command;
    long lastRun = 0;
    long lastClear = 0;
    long lastSendDeviceData = 0;
    int scanAfter = BT_DEFAULT_SCAN_INTERVAL;
    
    boolean sendAutoDiscovery = false;
    long lastSendAutoDiscovery = 0;
    String autoDiscoveryPrefix = "";
    // This is not the best place here. This object should not know this, but autodiscover must use it.
    // You mut not use any other place in the object
    String mqttBaseTopic = "";

    boolean detailedReport = false;

    boolean monitorObservedOnly = false; // Monitor devices only which are configured in the database

    boolean networkConnected = false; // Connected to the network (Wifi STA)

    LinkedList<Device> devices = LinkedList<Device>();
    LinkedList<int> devicesToRemove = LinkedList<int>();

    public:
        BlueTooth(Log& rlog, Led& led) : logger(rlog, "[BLUE]") {
            this -> led = &led;
        }

        void setup(Database &database, Signal<MQTTMessage> &mqttMessageSend, Signal<Device> &deviceChanged) {

            this -> mqttMessageSend = &mqttMessageSend;
            this -> deviceChanged = &deviceChanged;
            this -> database = &database;
           
            BLEDevice::init(BOARD_NAME);
            pBLEScan = BLEDevice::getScan(); //create new scan            
            pBLEScan->setAdvertisedDeviceCallbacks(this);
            pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
            pBLEScan->setInterval(100);
            pBLEScan->setWindow(99);  // less or equal setInterval value

            logger << BOARD_NAME " is initiated";

            // Prefill the device list with the user's devices
            // In case of accidently reboot it will send a "not_home" message if the device is gone meanwhile
            // Example:
            // Device is available -> system sends a "home" MQTT message as retained
            // Device is phisically gone (owner go to work) and before the microcontorller sends the "not_home" message microcontroller reboots (power outage)
            // MQTT retained message will be never changed and device stays at home in the MQTT system.
            // Solution:
            // Owner upload the device names which must be observed, so we prefill the list and on the next scan will set them as gone if the story happens above

            // Devices in this format: 317234b9d2d0;15172f81accc;d0e003795c50            
            fillDevices(database.getValueAsString(DB_DEVICES));
            // Parse the sting, split by ;

            // Auto discovery for Home Assistant is available.
            // Set it tru if the user enabled it
            sendAutoDiscovery = (database.getValueAsInt(DB_HA_AUTODISCOVERY) > 0) ? true : false;
            if (sendAutoDiscovery) {
                autoDiscoveryPrefix = database.getValueAsString(DB_HA_AUTODISCOVERY_PREFIX);
            }

            // This is not the best place here. This object should not know this, but autodiscover must use it.
            // You mut not use any other place in the object
            this -> mqttBaseTopic = this -> database -> getValueAsString(String(DB_MQTT_TOPIC_PREFIX), false) + MQTT_TOPIC;

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
                
                if (millis() - dev.lastSeen > BT_DEVICE_TIMEOUT || (long) millis() - (long) dev.lastSeen < 0) {

                    // Give another chance to the device to appear (Device has DEVICE_DROP_OUT_COUNT lives in the beginning)
                    dev.mark--;
                    dev.lastSeen = millis();

                    if (dev.mark == 0) {
                        logger << "Device is gone. MAC: " << dev.mac;
                        
                        // Virtually remove the device
                        dev.available = false;
                        dev.rssi = "0";
                        devices.set(i, dev);

                        // Send an MQTT message about this device is NOT at home
                        handleDeviceChange(dev);

                    } 
                    if (dev.mark > 0) {
                        logger << "Device marked as gone. MAC: " << dev.mac << " Current mark is: " << (String)dev.mark;
                        devices.set(i, dev);
                    }
                } 
            }
            
            if ((millis() - lastClear > BT_LIST_REBUILD_INTERVAL && devices.size() > 0) || (long) millis() - (long) lastClear < 0) {
                lastClear = millis();
                logger << "Clear the device list. (This is normal operation. :))";
                // Clear the list, it will be rebuilt again. Resend the (available) status should not be a problem.
                devices.clear();
                fillDevices(this-> database -> getValueAsString(DB_DEVICES));
                mqttMessageSend->fire(MQTTMessage{"selfclean", "true", false});
            }
            
            /*
            if (detailedReport) {
                if ((millis() - lastSendDeviceData > BT_DEVICE_DATA_INTERVAL && devices.size() > 0) || (long) millis() - (long) lastSendDeviceData < 0) {
                    lastSendDeviceData = millis();
                    logger << "Send device data.");
                    for (int i = 0; i < this -> devices.size(); i++) {
                        Device dev = devices.get(i);
                        if (dev.mac != NULL && dev.mac.length() > 0) {
                            String payload = "{\"name\":\"" + ((dev.name == NULL) ? "" : dev.name) + "\", \"rssi\":\"" + ((dev.rssi == NULL) ? "" : dev.rssi) + "\", \"mac\":\"" + ((dev.mac == NULL) ? "" : dev.mac) + "\", \"presence\":\"" + getPresentString(*database, dev.available) + "\", \"observed\":\"" + ((dev.observed) ? "true" : "false") + "\"}";
                            mqttMessageSend->fire(MQTTMessage{"status/" + dev.mac, payload, false});
                        }
                    }
                }
            }
            */

            if (sendAutoDiscovery) {
                if ((millis() - lastSendAutoDiscovery > HA_AUTODISCOVERY_INTERVAL && devices.size() > 0) || (long) millis() - (long) lastSendAutoDiscovery < 0) {
                    lastSendAutoDiscovery = millis();
                    logger << "Send autodicovery data.";
                    for (int i = 0; i < this -> devices.size(); i++) {
                        Device dev = devices.get(i);
                        // Example
                        // mosquitto_pub -h 127.0.0.1 -t home-assistant/device_tracker/a4567d663eaf/config -m '{"state_topic": "a4567d663eaf/state", "name": "My Tracker", "payload_home": "home", "payload_not_home": "not_home"}'
/*
{
              "name": "fd9be47e8fdc",
              "state_topic": "blecker/fd9be47e8fdc",
              "command_topic": "device_tracker/fd9be47e8fdc/",
              "payload_on": "1",
              "payload_off": "0",
              "state_on": "home",
              "state_off": "not_home",
              "unique_id": "fd9be47e8fdc",
              "device": {
                  "identifiers": ["fd9be47e8fdc"],
                  "name": "JBD fd9be47e8fdc",
                  "model": "JBD1582",
                  "manufacturer": "J-Style",
                  "sw_version": "1.X"
              }
            }
*/

                        if (dev.mac != NULL) {
                            String payload = "{\"state_topic\": \"" + mqttBaseTopic + "/" + dev.mac + "\", \"name\": \"" + dev.mac 
							+ "\", \"unique_id\": \"" +  dev.mac 
							+ "\", \"payload_home\": \"" +     this -> database->getValueAsString(DB_PRECENCE) 
							+ "\", \"payload_not_home\": \"" + this -> database->getValueAsString(DB_NO_PRECENCE) 
							+ "\", \"source_type\": \"bluetooth_le\"}";

                            MQTTMessage autoDiscMessage = MQTTMessage{autoDiscoveryPrefix + "/device_tracker/" + dev.mac + "/config", payload, false, true};
                            mqttMessageSend->fire(autoDiscMessage);
                        }
                    }
                }
            }

        }

        void setConnected(boolean connected) {
            this -> networkConnected = connected;
        }
private: 
        

        void onResult(BLEAdvertisedDevice advertisedDevice) {
            // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
            //logger << "Found device MAC: " + advertisedDevice.getAddress().toString().c_str());
            
            boolean newFound = true;
            String deviceMac = advertisedDevice.getAddress().toString().c_str();
            deviceMac.toLowerCase();
            String deviceName = advertisedDevice.getName().c_str();
            String deviceRSSI = (String) advertisedDevice.getRSSI();
            deviceMac.replace(":","");

            for (int i = 0; i < this -> devices.size(); i++) {
                Device dev = devices.get(i);
                if (deviceMac == dev.mac) {

                    // Device came back (state changed)
                    if (!dev.available) {
                        // Send an MQTT message about this device is at home
                        dev.available = true;
                        handleDeviceChange(dev);
                    }
                    dev.lastSeen = millis();
                    dev.mark = DEVICE_DROP_OUT_COUNT;
                    dev.available = true;
                    devices.set(i, dev);                   
                    newFound = false;
                }
            }

            if (!monitorObservedOnly) {
                if (newFound) {
                    Device dev = {deviceName, deviceRSSI, deviceMac, true, millis(), DEVICE_DROP_OUT_COUNT, false };
                    devices.add(dev);
                    logger << "New device found. MAC: " << deviceMac;
                    // Send an MQTT message about this device is at home
                    handleDeviceChange(dev);
                }
            }
        }

        void fillDevices(String devicesString) {

            if (devicesString.length() == 0) { return; }

            this->monitorObservedOnly = true;
            char *devicesChar = new char[devicesString.length() + 1];
            strcpy(devicesChar, devicesString.c_str());
            String devMac = "";
            while ((devMac = strtok_r(devicesChar, PARSE_CHAR, &devicesChar)) != NULL) { // delimiter is the semicolon
                if (devMac.length() > 0) {
                    devMac.toLowerCase();
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
                    logger << "Device added as observed device. MAC: " << devMac;
                }
            }

            delete [] devicesChar;
        }

        void handleDeviceChange(Device dev) {
            mqttMessageSend->fire(MQTTMessage{dev.mac, getPresentString(*database, dev.available), true});
            // TODO: need to refactor, send only one message for the consumers
            deviceChanged->fire(dev);

            if (detailedReport) {
                String payload = "{\"name\":\"" + ((dev.name == NULL) ? "" : dev.name) + "\", \"rssi\":\"" + ((dev.rssi == NULL) ? "" : dev.rssi) + "\", \"mac\":\"" + ((dev.mac == NULL) ? "" : dev.mac) + "\", \"presence\":\"" + getPresentString(*database, dev.available) + "\", \"observed\":\"" + ((dev.observed) ? "true" : "false") + "\"}";
                mqttMessageSend->fire(MQTTMessage{"status/" + dev.mac, payload, false});
            }
        }
};

#endif
