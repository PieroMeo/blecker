#ifndef LED
#define LED

#include "definitions.h"
#include <Arduino.h>
#include <jled.h>
#include "log.hpp"

class Led {

    Logger logger;
    int messageCode;    
    JLed* led;

    unsigned long lastrun = 0;
    
    public:
        Led(Log& rlog) : logger(rlog, "[LED]") {
            this -> messageCode = ERROR_NO_ERROR;
            this -> led = new JLed (LED_BUILTIN);
        }

        void setup () {
            
        }

        void loop() {            
            if (this -> messageCode != ERROR_NO_ERROR) {
                // Need update to "display" led pattern                
                this -> led -> Update();
                
                if (millis() - lastrun > 5000) {
                    if (!this -> led->IsRunning()) {                    
                        this->led -> Reset();                    
                        lastrun = millis();
                    }
                }
            }
            
        }

        // Message code can be a negative number.
        // If negative that means this message is removed
        void setMessage(int messageCode) {            
            // Prevent the continuous triggers
            if (this -> messageCode != messageCode) {
                logger << "Incoming error message: " << (String)messageCode;
                this -> messageCode = messageCode;
                if (this->messageCode != ERROR_NO_ERROR) {
                    this -> led -> Breathe(300).Repeat(this -> messageCode);
                    lastrun = millis();
                } else {
                    if (this -> led -> IsRunning()) {
                        this -> led-> Stop();
                        this -> led-> Reset();
                    }
                }
            }            
        }

    private:
     
};

#endif