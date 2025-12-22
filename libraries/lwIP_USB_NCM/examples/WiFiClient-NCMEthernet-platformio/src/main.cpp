/*
    This sketch establishes a TCP connection to a "quote of the day" service.
    It sends a "hello" message, and then prints received data.
*/

#include <Arduino.h>
#include <NCMEthernetlwIP.h>

const char* host = "djxmmx.net";
const uint16_t port = 17;

NCMEthernetlwIP eth;
IPAddress my_static_ip_addr(192, 168, 137, 100);
IPAddress my_static_gateway_and_dns_addr(192, 168, 137, 1);

//#define USE_REAL_UART

#if defined(USE_REAL_UART)
#define SER Serial1
#else
#define SER Serial
#endif

void setup() {
    // enable Serial1 so it can be used by USE_REAL_UART or by DEBUG_RP2040_PORT
    Serial1.end();
    Serial1.setTX(16);
    Serial1.setRX(17);
    Serial1.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);
    delay(3000);
    SER.println();
    SER.println();
    SER.println("Starting NCM Ethernet port");


    //optional static config
    eth.config(my_static_ip_addr, my_static_gateway_and_dns_addr, IPAddress(255, 255, 255, 0), my_static_gateway_and_dns_addr);

    // Start the Ethernet port
    // This starts DHCP in case config() was not called before
    bool ok = eth.begin();
    delay(1000);
    if (!ok) {
        while (1) {
            SER.println("Failed to initialize NCM Ethernet.");
            delay(1000);
        }
    } else {
        SER.println("NCM Ethernet started successfully.");
    }

}

void loop() {
    return;
    while(Serial.available()) {
        int b = Serial.read();
        Serial.print((char)b);
    }
}
