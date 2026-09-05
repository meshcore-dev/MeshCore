/**
 *   PsychicMqttClient
 *
 *   Simple Example for a minimal MQTT client using the PsychicMqttClient library.
 *
 *   Please change the ssid and pass to your actual WiFi credentials.
 *
 *   This example uses the public MQTT broker broker.hivemq.com,
 *   registers a onTopic callback function subscribed to a unique topic
 *   and publishes a message to that topic. The echoed message payload will be
 *   printed to the serial monitor.
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PsychicMqttClient.h>
#include <WiFiClientSecure.h>

const char ssid[] = "ssid"; // your network SSID (name)
const char pass[] = "pass"; // your network password

/**
 * Load the root certificate bundle embedded by the build process
 */
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_src_certs_x509_crt_bundle_bin_end");

/**
 * Create a PsychicMqttClient object
 */
PsychicMqttClient mqttClient;

/**
 * Create a WiFiClientSecure object
 */
WiFiClientSecure client;
const char *server = "www.howsmyssl.com";

void fetchData()
{
    Serial.println("\nStarting connection to server...");
    if (!client.connect(server, 443))
        Serial.println("Connection failed!");
    else
    {
        Serial.println("Connected to server!");
        // Make a HTTP request:
        client.println("GET https://www.howsmyssl.com/a/check HTTP/1.0");
        client.println("Host: www.howsmyssl.com");
        client.println("Connection: close");
        client.println();

        while (client.connected())
        {
            String line = client.readStringUntil('\n');
            if (line == "\r")
            {
                Serial.println("headers received");
                break;
            }
        }
        // if there are incoming bytes available
        // from the server, read them and print them:
        while (client.available())
        {
            char c = client.read();
            Serial.write(c);
        }

        client.stop();
        Serial.println("\nStopping connection to server...");
    }
}

void setup()
{
    Serial.begin(115200);

    /**
     * Connect to the WiFi network with the given ssid and pass.
     */
    WiFi.begin(ssid, pass);

    Serial.printf("Connecting to WiFi %s .", ssid);
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.printf("\r\nConnected, IP address: %s \r\n", WiFi.localIP().toString().c_str());

    /**
     * Attach the root certificate bundle to the WiFiClientSecure object
     * first and fetch some data from a server.
     */
#if ESP_ARDUINO_VERSION_MAJOR == 3
    client.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start);
#else
    client.setCACertBundle(rootca_crt_bundle_start);
#endif
    fetchData();

    /**
     * Connect to the open MQTT broker broker.hivemq.com with SSL/TLS enryption.
     */
    mqttClient.setServer("mqtts://broker.hivemq.com");
    mqttClient.attachArduinoCACertBundle();

    /**
     * Lambda callback function for onTopic Event Handler
     *
     * Subscribes to the topic "{MAC-Address}/simple" with QoS 0.
     * The lambda callback function will be called when a message is received.
     */
    String topic = String(ESP.getEfuseMac()) + "/simple";

    mqttClient.onTopic(topic.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       {
                        /**
                         * Using a lambda callback function is a very convenient way to handle the 
                         * received message. The function will be called when a message is received. 
                         * 
                         * The parameters are:
                         * - topic: The topic of the received message
                         * - payload: The payload of the received message
                         * - retain: The retain flag of the received message
                         * - qos: The qos of the received message
                         * - dup: The duplicate flag of the received message
                         * 
                         * It is important not to do any heavy calculations, hardware access, delays or 
                         * blocking code in the callback function.
                        */
                        Serial.printf("Received Topic: %s\r\n", topic);
                        Serial.printf("Received Payload: %s\r\n", payload); });

    /**
     * Connect to the MQTT broker
     */
    mqttClient.connect();

    /**
     * Wait blocking until the connection is established
     */
    while (!mqttClient.connected())
    {
        delay(500);
    }

    /**
     * Publish a message to the topic "{MAC-Address}/simple" with QoS 0 and retain flag 0.
     *
     * You can only publish messages after the connection is established.
     */
    mqttClient.publish(topic.c_str(), 0, 0, "Hello World!");
}

void loop()
{
    /**
     * Nothing to do here, the onTopic callback function will be called when a message is received.
     */
}
