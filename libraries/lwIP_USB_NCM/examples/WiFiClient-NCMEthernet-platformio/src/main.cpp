/*
    This sketch establishes a TCP connection to a "quote of the day" service.
    It sends a "hello" message, and then prints received data.
*/

#include <Arduino.h>
#include <NCMEthernetlwIP.h>
#include <USB.h>

const char* host = "djxmmx.net";
const uint16_t port = 17;

NCMEthernetlwIP eth;
IPAddress my_static_ip_addr(192, 168, 137, 100);
IPAddress my_static_gateway_and_dns_addr(192, 168, 137, 1);

#define USE_REAL_UART

#if defined(USE_REAL_UART)
#define SER Serial1
#else
#define SER Serial
#endif

void setup() {
    // enable Serial1 so it can be used by USE_REAL_UART or by DEBUG_RP2040_PORT
    pinMode(18, OUTPUT);
    pinMode(19, OUTPUT);
    pinMode(20, OUTPUT);
    pinMode(21, OUTPUT);

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
    delay(100);
    if (!ok) {
        while (1) {
            SER.println("Failed to initialize NCM Ethernet.");
            delay(1000);
        }
    } else {
        SER.println("NCM Ethernet started successfully.");
    }

}

void printstats() {
    SER.print("xmit ");
    SER.print(eth_stats.xmit_enqueued);
    SER.print(" recv ");
    SER.print(eth_stats.recv_enqueued);
    SER.print(" ");

    if(eth_stats.xmit_enqueued!=eth_stats.xmit_dequeued) {
        SER.print("xmit enqueued ");
        SER.print(eth_stats.xmit_enqueued);
        SER.print(" dequeued ");
        SER.print(eth_stats.xmit_enqueued);
        SER.print(" ");
    }
    if(eth_stats.xmit_queue_full>0) {
        SER.print("xmit queue full ");
        SER.print(eth_stats.xmit_queue_full);
        SER.print(" ");
    }
    if(eth_stats.xmit_usb_mutex_blocked>0) {
        SER.print("xmit usb mutex blocked ");
        SER.print(eth_stats.xmit_usb_mutex_blocked);
        SER.print(" ");
    }
    if(eth_stats.xmit_tud_not_ready>0) {
        SER.print("xmit tud not ready ");
        SER.print(eth_stats.xmit_tud_not_ready);
        SER.print(" ");
    }
    if(eth_stats.xmit_queue_full>0) {
        SER.print("xmit queue full ");
        SER.print(eth_stats.xmit_queue_full);
        SER.print(" ");
    }

    if(eth_stats.recv_enqueued!=eth_stats.recv_dequeued) {
        SER.print("recv enqueued ");
        SER.print(eth_stats.recv_enqueued);
        SER.print(" dequeued ");
        SER.print(eth_stats.recv_dequeued);
        SER.print(" ");
    }
    if(eth_stats.recv_queue_full>0) {
        SER.print("recv queue full ");
        SER.print(eth_stats.recv_queue_full);
        SER.print(" ");
    }
    if(eth_stats.recv_discarded>0) {
        SER.print("recv discard ");
        SER.print(eth_stats.recv_discarded);
        SER.print(" ");
    }

    eth_stats.xmit_queue_full = 0;
    eth_stats.xmit_enqueued = 0;
    eth_stats.xmit_usb_mutex_blocked = 0;
    eth_stats.xmit_tud_not_ready = 0;
    eth_stats.xmit_dequeued = 0;

    eth_stats.recv_queue_full = 0;
    eth_stats.recv_enqueued = 0;
    eth_stats.recv_dequeued = 0;
    eth_stats.recv_discarded = 0;

    SER.print("usb irq ");
    SER.print(usb_stats.usb_irq_called);
    SER.print(" ");
    if(usb_stats.usb_mutex_blocked>0) {
        SER.print("mutex blocked ");
        SER.print(usb_stats.usb_mutex_blocked);
        SER.print(" ");
    }

    usb_stats.usb_irq_called = 0;
    usb_stats.usb_mutex_blocked = 0;
}

void loop() {
    static unsigned long next_msg = 0;
    static bool led_on = false;
    if (millis() > next_msg) {
        // SER.println(".");
        next_msg = millis() + 1000;
        digitalWrite(LED_BUILTIN, led_on);
        led_on ^= 1;
    }
    printstats();

    static bool connected = false;
    if (!eth.connected()) {
        connected = false;
        return;
    } else if (!connected) {
        SER.println("");
        SER.println("Ethernet connected");
        SER.println("IP address: ");
        SER.println(eth.localIP());
#if LWIP_IPV6
        for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
            IPAddress address = IPAddress(&eth.getNetIf()->ip6_addr[i]);
            if (!address.isSet()) {
                continue;
            }
            SER.println(address);
        }
#endif
        connected = true;
    }

    static bool wait = false;

    SER.printf("connecting to %s:%i ...", host, port);

    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    debug_put(CLIENT_CONNECT, true);
    if (!client.connect(host, port)) {
        debug_put(CLIENT_CONNECT, false);
        SER.println("connection failed");
        delay(500);
        return;
    }
    debug_put(CLIENT_CONNECT, false);

    // This will send a string to the server
    SER.print("sending data to server...");
    if (client.connected()) {
        debug_put(CLIENT_PRINTLN, true);
        client.println("hello from NCM RP2040");
        debug_put(CLIENT_PRINTLN, false);
    }

    // wait for data to be available
    unsigned long timeout = millis();
    while (true) {
        debug_put(CLIENT_AVAILABLE, true);
        if(client.available() != 0) {
            debug_put(CLIENT_AVAILABLE, false);
            break;
        }
        debug_put(CLIENT_AVAILABLE, false);

        if (millis() - timeout > 5000) {
            SER.println(">>> Client Timeout !");
            debug_put(CLIENT_STOP, true);
            client.stop();
            debug_put(CLIENT_STOP, false);
            delay(500);
            return;
        }
    }

    // Read all the lines of the reply from server and print them to Serial
    // SER.println("receiving from remote server");
    // not testing 'client.connected()' since we do not need to send data here
    int i = 0;
    while (true) {
        debug_put(CLIENT_AVAILABLE, true);
        if(client.available() == 0) {
            break;
        }
        debug_put(CLIENT_AVAILABLE, false);
        debug_put(CLIENT_READ, true);
        char ch = static_cast<char>(client.read());
        debug_put(CLIENT_READ, false);
        if(i++<10)
            SER.print(ch);
    }

    // Close the connection
    //SER.println();
    SER.println("closing connection");
    client.stop();

    if (wait) {
        delay(500);
    }
    wait = true;
}
