/**
 *   PsychicMqttClient
 *
 *   Fully Featured Example for a minimal MQTT client using the PsychicMqttClient library.
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

const char ssid[] = "ssid"; // your network SSID (name)
const char pass[] = "pass"; // your network password

/**
 * Load the root certificate bundle embedded by the PIO build process
 */
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_src_certs_x509_crt_bundle_bin_end");

const char loremIpsum[] = "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, "
                          "sed diam nonumy eirmod tempor invidunt ut labore et dolor"
                          "e magna aliquyam erat, sed diam voluptua.At vero eos et a"
                          "ccusam et justo duo dolores et ea rebum.Stet clita kasd g"
                          "ubergren, no sea takimata sanctus est Lorem ipsum dolor s"
                          "it amet.Lorem ipsum dolor sit amet, consetetur sadipscing"
                          " elitr, sed diam nonumy eirmod tempor invidunt ut labore "
                          "et dolore magna aliquyam erat, sed diam voluptua.At vero "
                          "eos et accusam et justo duo dolores et ea rebum.Stet clit"
                          "a kasd gubergren, no sea takimata sanctus est Lorem ipsum"
                          " dolor sit amet.Lorem ipsum dolor sit amet, consetetur sa"
                          "dipscing elitr, sed diam nonumy eirmod tempor invidunt ut"
                          " labore et dolore magna aliquyam erat, sed diam voluptua."
                          "At vero eos et accusam et justo duo dolores et ea rebum.S"
                          "tet clita kasd gubergren, no sea takimata sanctus est Lor"
                          "em ipsum dolor sit amet.Duis autem vel eum iriure dolor i"
                          "n hendrerit in vulputate velit esse molestie consequat, v"
                          "el illum dolore eu feugiat nulla facilisis at vero eros e"
                          "t accumsan et iusto odio dignissim qui blandit praesent l"
                          "uptatum zzril delenit augue duis dolore te feugait nulla "
                          "facilisi. Lorem ipsum dolor sit amet, consectetuer adipis"
                          "cing elit, sed diam nonummy nibh euismod tincidunt ut lao"
                          "reet dolore magna aliquam erat volutpat.Ut wisi enim ad m"
                          "inim veniam, quis nostrud exerci tation ullamcorper susci"
                          "pit lobortis nisl ut aliquip ex ea commodo consequat.Duis"
                          " autem vel eum iriure dolor in hendrerit in vulputate vel"
                          "it esse molestie consequat, vel illum dolore eu feugiat n"
                          "ulla facilisis at vero eros et accumsan et iusto odio dig"
                          "nissim qui blandit praesent luptatum zzril delenit augue "
                          "duis dolore te feugait nulla facilisi.Nam liber tempor cu"
                          "m soluta nobis eleifend option congue nihil imperdiet dom"
                          "ing id quod mazim placerat facer possim assum.Lorem ipsum"
                          " dolor sit amet, consectetuer adipiscing elit, sed diam n"
                          "onummy nibh euismod tincidunt ut laoreet dolore magna ali"
                          "quam erat volutpat.Ut wisi enim ad minim veniam, quis nos"
                          "trud exerci tation ullamcorper suscipit lobortis nisl ut "
                          "aliquip ex ea commodo consequat.Duis autem vel eum iriure"
                          " dolor in hendrerit in vulputate velit esse molestie cons"
                          "equat, vel illum dolore eu feugiat nulla facilisis.At ver"
                          "o eos et accusam et justo duo dolores et ea rebum.Stet cl"
                          "ita kasd gubergren, no sea takimata sanctus est Lorem ips"
                          "um dolor sit amet.Lorem ipsum dolor sit amet, consetetur "
                          "sadipscing elitr, sed diam nonumy eirmod tempor invidunt "
                          "ut labore et dolore magna aliquyam erat, sed diam voluptu"
                          "a.(End of Lorem Ipsum)";

/**
 * Create a PsychicMqttClient object
 */
PsychicMqttClient mqttClient;

void onMqttConnect(bool sessionPresent)
{
    Serial.println("Connected to MQTT.");
    Serial.printf("Session present: %d\r\n", sessionPresent);

    int packetIdSub = mqttClient.subscribe("test/lol", 2);
    Serial.printf("Subscribing at QoS 2, packetId: %d\r\n", packetIdSub);

    mqttClient.publish("test/lol", 0, true, "test 1");
    Serial.println("Publishing at QoS 0");

    int packetIdPub1 = mqttClient.publish("test/lol", 1, true, "test 2");
    Serial.printf("Publishing at QoS 1, packetId: %d\r\n", packetIdPub1);

    int packetIdPub2 = mqttClient.publish("test/lol", 2, true, "test 3");
    Serial.printf("Publishing at QoS 2, packetId: %d\r\n", packetIdPub2);
}

void onMqttDisconnect(bool sessionPresent)
{
    Serial.println("Disconnected from MQTT.");
}

void onMqttSubscribe(uint16_t packetId)
{
    Serial.println("Subscribe acknowledged.");
    Serial.printf("  packetId: %d\r\n", packetId);
}

void onMqttUnsubscribe(uint16_t packetId)
{
    Serial.println("Unsubscribe acknowledged.");
    Serial.printf("  packetId: %d\r\n", packetId);
}

void onMqttMessage(char *topic, char *payload, int retain, int qos, bool dup)
{
    Serial.println("Message received.");
    Serial.printf("  topic: %s\r\n", topic);
    Serial.printf("  qos: %d\r\n", qos);
    Serial.printf("  dup: %d\r\n", dup);
    Serial.printf("  retain: %d\r\n", retain);
}

void onMqttPublish(uint16_t packetId)
{
    Serial.println("Publish acknowledged.");
    Serial.printf("  packetId: %d\r\n", packetId);
}

void onLongTopic(const char *topic, const char *payload, int retain, int qos, bool dup)
{
    Serial.printf("Received Topic: %s\r\n", topic);
    Serial.printf("Received Payload: %s\r\n", payload);
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
     * Connect to the open MQTT broker broker.hivemq.com with SSL/TLS enryption.
     */
    mqttClient.setServer("mqtts://broker.hivemq.com");

#if ESP_ARDUINO_VERSION_MAJOR == 3
    mqttClient.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start);
#else
    mqttClient.setCACertBundle(rootca_crt_bundle_start);
#endif

    mqttClient.setBufferSize(1024);

    /**
     * Lambda callback function for onTopic Event Handler
     *
     * Subscribes to the topic "{MAC-Address}/simple" with QoS 0.
     * The lambda callback function will be called when a message is received.
     */
    String simpleTopic = String(ESP.getEfuseMac()) + "/simple";
    String longTopic = String(ESP.getEfuseMac()) + "/long";

    /**
     * Register the callback functions for the MQTT client
     */
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);

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
    mqttClient.onTopic(simpleTopic.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       {
        Serial.printf("Received Topic: %s\r\n", topic);
        Serial.printf("Received Payload: %s\r\n", payload); });

    /**
     * Using a dedicated callback function is a good way to handle the received message.
     */
    mqttClient.onTopic(longTopic.c_str(), 0, onLongTopic);

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
    mqttClient.publish(simpleTopic.c_str(), 0, 0, "Hello World!");

    delay(5000);
    Serial.println("Unsubscribing from topic test/lol");
    mqttClient.unsubscribe("test/lol");

    delay(5000);
    Serial.println("Publishing lorem ipsum...");
    mqttClient.publish(longTopic.c_str(), 0, 0, loremIpsum);
}

void loop()
{
    /**
     * Nothing to do here, the onTopic callback function will be called when a message is received.
     */
}
