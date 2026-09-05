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

#define RECEIVE_DELAY 5000

const char ssid[] = "ssid"; // your network SSID (name)
const char pass[] = "pass"; // your network password

PsychicMqttClient mqttClient;

/*
    Setup a number of topics with and without wildcards.
*/
const String TestTopicA = String(ESP.getEfuseMac()) + "/test/A/0000/topic";
const String TestTopicAWildcard = String(ESP.getEfuseMac()) + "/test/A/+/topic";
const String TestTopicAReference = String(ESP.getEfuseMac()) + "/test/A/1111/topic";
const String TestTopicALong = String(ESP.getEfuseMac()) + "/test/A/0000/topic/long";

const String TestTopicB = String(ESP.getEfuseMac()) + "/test/B/topic/0000";
const String TestTopicBWildcard = String(ESP.getEfuseMac()) + "/test/B/topic/#";
const String TestTopicBReference = String(ESP.getEfuseMac()) + "/test/B/topic/1111";
const String TestTopicBLong = String(ESP.getEfuseMac()) + "/test/B/topic/0000/long";

const String TestTopicC = String(ESP.getEfuseMac()) + "/test/C/topic/0000";
const String TestTopicCWildcard = String(ESP.getEfuseMac()) + "/test/C/topic/+";
const String TestTopicCReference = String(ESP.getEfuseMac()) + "/test/C/topic/1111";
const String TestTopicCLong = String(ESP.getEfuseMac()) + "/test/C/topic/0000/long";

const String TestAllTopics = String(ESP.getEfuseMac()) + "/test/#";
const String TestTopic = String(ESP.getEfuseMac()) + "/test/topic";

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, pass);

    Serial.printf("Connecting to WiFi %s .", ssid);
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.printf("\r\nConnected, IP address: %s \r\n", WiFi.localIP().toString().c_str());

    mqttClient.setServer("mqtt://broker.hivemq.com");

    Serial.printf("Subscribing to %s\r\n", TestTopicA.c_str());
    mqttClient.onTopic(TestTopicA.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicA.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestTopicAWildcard.c_str());
    mqttClient.onTopic(TestTopicAWildcard.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicAWildcard.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestTopicB.c_str());
    mqttClient.onTopic(TestTopicB.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicB.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestTopicBWildcard.c_str());
    mqttClient.onTopic(TestTopicBWildcard.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicBWildcard.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestTopicC.c_str());
    mqttClient.onTopic(TestTopicC.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicC.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestTopicCWildcard.c_str());
    mqttClient.onTopic(TestTopicCWildcard.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestTopicCWildcard.c_str(), topic); });

    Serial.printf("Subscribing to %s\r\n", TestAllTopics.c_str());
    mqttClient.onTopic(TestAllTopics.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       { Serial.printf("Received %s: %s\r\n", TestAllTopics.c_str(), topic); });

    mqttClient.connect();

    while (!mqttClient.connected())
    {
        delay(500);
    }

    delay(RECEIVE_DELAY);
}

void loop()
{
    Serial.println("Testing Wildcards:");

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicA.c_str());
    mqttClient.publish(TestTopicA.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicAReference.c_str());
    mqttClient.publish(TestTopicAReference.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicALong.c_str());
    mqttClient.publish(TestTopicALong.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicB.c_str());
    mqttClient.publish(TestTopicB.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicBReference.c_str());
    mqttClient.publish(TestTopicBReference.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicBLong.c_str());
    mqttClient.publish(TestTopicBLong.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicC.c_str());
    mqttClient.publish(TestTopicC.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicCReference.c_str());
    mqttClient.publish(TestTopicCReference.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.printf("\r\nPublishing to %s\r\n", TestTopicCLong.c_str());
    mqttClient.publish(TestTopicCLong.c_str(), 0, 0, "test");
    delay(RECEIVE_DELAY);

    Serial.println("\r\nTest done.");
    vTaskDelete(NULL);
}
