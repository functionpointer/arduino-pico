/*
    This sketch establishes a TCP connection to a "quote of the day" service.
    It sends a "hello" message, and then prints received data.
*/

#include <Arduino.h>
#include <NCMEthernetlwIP.h>

const char* host = "djxmmx.net";
const uint16_t port = 17;

NCMEthernetlwIP eth;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println();
  Serial.println("Starting NCM Ethernet port");

  // Start the Ethernet port
  if (!eth.begin()) {
    Serial.println("Failed to initialize NCM Ethernet.");
    while (1) {
      delay(1000);
    }
  }
  
}

void loop() {
  static unsigned long next_msg = 0;
  static bool led_on = false;
  if(millis() > next_msg) {
    Serial.println(".");
    next_msg = millis() + 1000;
    digitalWrite(LED_BUILTIN, led_on);
    led_on ^=1;
  }

  static bool connected = false;
  if(!eth.connected()) {
    connected = false;
    return;
  } else if(!connected){
    Serial.println("");
    Serial.println("Ethernet connected");
    Serial.println("IP address: ");
    Serial.println(eth.localIP());
    connected = true;
  }

  static bool wait = false;

  Serial.print("connecting to ");
  Serial.print(host);
  Serial.print(':');
  Serial.println(port);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    delay(5000);
    return;
  }

  // This will send a string to the server
  Serial.println("sending data to server");
  if (client.connected()) {
    client.println("hello from RP2040");
  }

  // wait for data to be available
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      delay(60000);
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  Serial.println("receiving from remote server");
  // not testing 'client.connected()' since we do not need to send data here
  while (client.available()) {
    char ch = static_cast<char>(client.read());
    Serial.print(ch);
  }

  // Close the connection
  Serial.println();
  Serial.println("closing connection");
  client.stop();

  if (wait) {
    delay(300000);  // execute once every 5 minutes, don't flood remote service
  }
  wait = true;
}
