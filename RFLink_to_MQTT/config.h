const char* ssid = "SSID"; // network SSID for ESP8266 to connect to
const char* password = "key"; // password for the network above
const char* mqtt_server = "192.168.1.140"; // address of the MQTT server that we will communicte with
const char* mqtt_user = "pi"; // username for MQTT
const char* mqtt_password = "raspberry"; // password for MQTT
char* client_name = "espRF"; // production version client name for MQTT login - must be unique on your system
// some testing switches
boolean testmode = false; // if true, then do not listen to softwareserial but normal serial for input
boolean enableMQTT = true; // if false, do not transmit MQTT codes - for testing really
boolean enableDebug = true; // if false, do not send data to debug topic - for testing really
