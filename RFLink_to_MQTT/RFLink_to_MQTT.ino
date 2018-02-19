/*
Link between the RF reciever/transmitter project based on an arduino mega, RFLink and MQTT
Link to RFLink: http://www.nemcon.nl/blog2/download
This includes wiring and device compatability for RFLink. 
This is used as per the RFLink instructions except that 
 an ESP8266 is used for the serial communication instead of a computer via USB


RFLink is designed to be used by a computer using USB
This bridge uses and ESP8266 as the interface, which then encodes the recieved RF data and publishes it as JSON to an MQTT server

RF433 data is recieved by the RFLink and passed on to the ESP8266
The ESP8266 packages it up into a JSON statement and publishes it ot the MQTT broker
format on the MQTT broker is: (Recieved RF codes)

Topic: RF/name_of_device-ID_of_device     - this tries to have a unique topic per name/id combo. 
    Note - the name and ID are as determined by the RFLink program - it may not be the label printed on the device sending data!

Payload example: {"raw":"20;B3;DKW2012;ID=004c;TEMP=00eb;HUM=3f;WINSP=0000;WINGS=0000;RAIN=0003;WINDIR=0008;\r","TEMP":23.50,"HUM":3,"WINSP":"0000","WINGS":"0000","RAIN":3,"WINDIR":8}
see RFLink documentation for more details: http://www.nemcon.nl/blog2/protref

Sending commands to the RFLink example: (how to send codes to the RFLink)
Topic: RF/command
Payload: 10;Eurodomest;02d0f2;06;ALLON\n
    Note that the \n on the end is critical at the moment. Otherwise the ESP will either crash or the RFLink will ignore



I inculde the raw data in full for debugging, then the split out components form RFink
There is some conversions made to make the data more useable:
    Temp is converted to a float from hex
    Wind direction is converted to a compass heading
    etc - details in the parseData function below


Requirements:
Arduino mega 2560 and RF reciever and or transmitter (see RFLink for recommended devices and wiring)
ESP8266 device (I have used a node MCU V1.0)
Arduino libraries: SoftwareSerial.h and ArduinoJson.h
an MQTT broker runnning. Tested with Mosquitto on a raspberry Pi 2

Optional: 
Somethig to read and react to the MQTT measurements
I am using Home Assistant, also running on the same Pi : https://home-assistant.io/


Setup:
1) Confirm RFLink working on its own through your USB port - then you know you are getting data recieved etc
    Collect some data form your RF433 devices on the system monitor screen and save them for step 2

2) Set sketch to be in test mode (testmode = true). 
   Load this firmware onto the ESP8266 and run in test mode (so it listens to the PC over USB, not the RFLink)
    Input some data as recieved in step 1. Check that the ESP connects to your system and publishes topics as expected

3) Load updated firmware - setting testmode to false (see section for user inputs below)
    Wire the ESP to the RFLink ESP d5 & d6  to Mega 0 & 1.
    Check your mqtt broker for recieved data being published.
 
4) Setup your home automation to react to the published data and pubish commands to the RFLink 
      
To Do:
1) ESP will sometimes crash with unexpected output from RFLink. 
    I think it is when you get messages starting with 20 but not with as as many semicolon delimited fields as expected.
    Currently, I ensure that the Mega (RFLink) is up before restarting the ESP.

2) Tidy up the callback behaviour for sending data to the RFLink - data is fickle and if you do not terminate with \n it will crash the ESP
  
  
Phil Wilson December 2016

*/
#include <SoftwareSerial.h>

// Key User Configuration here:
SoftwareSerial swSer(14, 12, false, 256); // d5 & d6 on the nodu MC v1.0


#include "config.h"

// ******************************************************************



// ArduinoJson credits - used for building JSON to post to MQTT

// Copyright Benoit Blanchon 2014-2016
// MIT License
//
// Arduino JSON library
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>



const byte numChars = 128;
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use when parsing

      // variables to hold the parsed data

char messageFromPC[numChars] = {0};
char RFName[30]; // name of protocol from RFLINK
char RFID[30]; //ID from RFLINK ie ID=XXXX;
char RFData[numChars]; //the rest from RFLINK - will include one or many pieces of data
char RFDataTemp[numChars]; //temporary area for processing RFData - when we look for temp and convert etc
String MQTTTopic = "RF/";
const char* willTopic = "RF/status";
boolean willRetain = true;
const char* willMessage = "offline" ;

const char* commandTopic = "RF/command";  // command topic ESP will subscribe to and pass as commands to the RFLink
const char* debugTopic = "RF/DEBUG";  // DEBUG topic ESP will pass as Debug messages

const float TempMax = 50.0; // max temp - if we get a value greater than this, ignore it as an assumed error
const int HumMax = 101; // max hum - if we get a value greater than this, ignore it as an assumed error


//============
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// if having problems with larger payloads, increase #define MQTT_MAX_PACKET_SIZE 128 in PubSubClient.h to a larger value before compiling
// to allow larger payloads - needed if you have a weather station (because I also include raw data in the json payload in case you need it for deugging) 
// increased to 256 due to weatherstation dk2012 issues
#include <PubSubClient.h>


WiFiClient espClient;
PubSubClient client(espClient);

boolean newData = false;
String switch1;
String strTopic;
char* strPayload ="";
String strOutputt = "";
String strOutputth = "";

// array of wind directions - used by weather stations. output from the RFLink under WINDIR is an integr 0-15 - will lookup the array for the compass text version
String CompassDirTable[17] = {"N","NNE","NE","ENE","E","ESE", "SE","SSE","S","SSW","SW","WSW", "W","WNW","NW","NNW","N"}; 

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0'; // terminate payload
  strPayload = ((char*)payload);
  char* strPayloadTrimmed = strPayload + 1; // strip off first character as it is a double quote
  String strPayloadTrimmed2 = strPayloadTrimmed + 1;
  
    Serial.println("Command coming in!: "); // got someting
    // strPayload += "\n";
    Serial.println(strPayload); // got something
    swSer.print(strPayload);   // snd data to the RFLink  
    swSer.print("\r\n");   // snd data to the RFLink    
    if(strncmp(strPayloadTrimmed,"10",2) == 0) // starts with 10
    {
      Serial.println("got a command - test result: ");
      Serial.println(strPayload); 
      Serial.println(sizeof(strPayload)); 
      Serial.println(strPayloadTrimmed2.length());
      strPayloadTrimmed2.remove(strPayloadTrimmed2.length()-1,1);
      Serial.println(strPayloadTrimmed[strPayloadTrimmed2.length()]);
      Serial.println(strPayloadTrimmed2);  
      if (enableDebug == true ){
        client.publish(debugTopic,strPayload,true);
      }
      swSer.print(strPayload);   // snd data to the RFLink      
    }

}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(client_name, mqtt_user, mqtt_password, willTopic, 0, willRetain, willMessage)) {
      Serial.println("connected");
      // Once connected, update status to online - will Message will drop in if we go offline ...
      client.publish(willTopic,"online",true); 
          
      client.subscribe(commandTopic);// subscribe to the command topic - will listen here for comands to the RFLink
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
if (testmode){client_name = "espRFTest3";} // in test mode - change client name to be different from production
// JSON parsing library setup
  DynamicJsonBuffer  jsonBuffer;

  // Create the root of the object tree.
  //
  // It's a reference to the JsonObject, the actual bytes are inside the
  // JsonBuffer with all the other nodes of the object tree.
  // Memory is freed when jsonBuffer goes out of scope.
  JsonObject& root = jsonBuffer.createObject();
  
  Serial.begin(57600);
  swSer.begin(57600); // this is the baud rate of the RF LINK
    // expected format examples: - will treat as 5 components (Packet Count ignored).
    // 20;02;Imagintronix;ID=0001;TEMP=00dc;HUM=88; 
    // 20;0D;UPM_Esic;ID=0001;TEMP=00df;HUM=67;BAT=OK;
   
    // Node [nn]: 20 - means message from system - proceed if prefix is 20. Other values are 10 for sent message and 11 for echo
    // Packet Count [hh]: next is packet count - ignore (2 digit hexdecimal)
    // Name [text]:  Name of the protocol used
    // ID [ID=text]: ID - proceed if ID indentified. If not there, is not a recieved code, just a message from system
    // Data [text]: remainder is data from the sensor - concateate & send as a JSON data block
 
    setup_wifi(); 
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
  
    Serial.println("\nRFLINK serial listener started");
    Serial.println("Example data  20;0D;UPM_Esic;ID=test;TEMP=00df;HUM=67;BAT=OK;");
    Serial.println("Example data  20;B3;DKW2012;ID=test;TEMP=00eb;HUM=3f;WINSP=008c;WINGS=00cd;RAIN=0003;WINDIR=0008;");
    Serial.println();

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(client_name);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("oTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
// end OTA stuff in setup
}




//============

void recvWithStartEndMarkers() {
    static byte ndx = 0;
    char endMarker = '\n';
    char rc;

    if (testmode == false) { // we are in live mode and will parse from swSer rather than serial
      while (swSer.available() > 0 && newData == false) {
      rc = swSer.read();
              if (rc != endMarker) {
                  if (isAscii(rc)) { // ensure char is ascii, this is to stop bad chars being sent https://www.arduino.cc/en/Tutorial/CharacterAnalysis
                    receivedChars[ndx] = rc;
                    ndx++;
                    if (ndx >= numChars) {
                        ndx = numChars - 1;
                    }
                  }
              }
              else {
                  receivedChars[ndx] = '\0'; // terminate the string
                  ndx = 0;
                  newData = true;
              }
     }
    } else { // test use - read from serial as though it was from swSer

 
//
        while (Serial.available() > 0 && newData == false) {
      rc = Serial.read();
    
                if (rc != endMarker) {
                    if (isAscii(rc)) { // ensure char is ascii, this is to stop bad chars being sent https://www.arduino.cc/en/Tutorial/CharacterAnalysis
                    receivedChars[ndx] = rc;
                    ndx++;
                    if (ndx >= numChars) {
                        ndx = numChars - 1;
                    }
                  }
                }
                else {
                    receivedChars[ndx] = '\0'; // terminate the string
                    ndx = 0;
                    newData = true;
                }
        }
        

    }
//

}

//============

float hextofloat(char* hexchars) {return float(strtol(hexchars,NULL,16));}
int hextoint(char* hexchars) {return strtol(hexchars,NULL,16);}

void parseData() {      // split the data into its parts
  DynamicJsonBuffer  jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

    char * strtokIndx; // this is used by strtok() as an index
    char * strtokIndx2; // this is used by strtok() as an index
    float tmpfloat = 0.0; // temporary float used in tests
    int tmpint = 0; // temporary int used in tests

    strtokIndx = strtok(tempChars,";");      // get the first part - the string
    strcpy(messageFromPC, strtokIndx); // copy it to messageFromPC
    if (strcmp(messageFromPC,"20") == 0 ) { // 20 means a message recieved from RFLINK - this is what we are interested in breaking up for use
      
      strtokIndx = strtok(NULL, ";" ); // this continues where the previous call left off - which we will ignore as it is the packet count
      strtokIndx = strtok(NULL, ";" ); // this should be the name
      strcpy( RFName , strtokIndx ); // copy name to RFName
      
      strtokIndx = strtok(NULL, ";");
      strcpy( RFID , strtokIndx); // copy next block to RFID    
  
      strtokIndx = strtok(NULL, "\n"); // search for the rest of the buffer input ending in \n
      strcpy(RFData , strtokIndx ); // copy remainder of block to RFData 
        // now find if we have TEMP= in RFData and convert from hex to int
      strcpy(RFDataTemp , RFData ); // temp copy of RFData so we can search for temp and convert from hex to decimal

        // Read each command pair 
        char* command = strtok(RFDataTemp, ";");
        root["raw"] = receivedChars; // copy the raw data to the json in case we need to debug
        while (command != 0 and strlen(command)>4 )
        {
            // Split the command in two values
            char* separator = strchr(command, '=');
            if (separator != 0)
            {
                // Actually split the string in 2: replace '=' with 0
                *separator = 0;
                String NamePart = command;
                ++separator;
                if (NamePart == "TEMP") { // test if it is TEMP, which is HEX
                  char negativeTemp[2];
                  size_t destination_size = sizeof (negativeTemp);
                  strncpy(negativeTemp, separator, destination_size);
                  negativeTemp[destination_size - 1] = '\0';
                  strncpy(separator, negativeTemp, 1);               
                  if (strcmp(negativeTemp,"8") == 0) { // if first char is a 8 then strip it off as this is a negative temp   
                    separator = separator + 1 ; //moving the pointer forward to remove first char 
                    tmpfloat=(hextofloat(separator)*-1)*0.10;  // convert from hex to float and multiply by minus 1 to invert and divide by 10 - using multiply as it is faster than divide               
                    if (enableDebug == true ){
                      client.publish(debugTopic,"Negative Temp",true);
                    }
                  }
                  else if (strcmp(negativeTemp,"0") == 0){ //  else it's a positve temp    
                    separator = separator + 1 ; //moving the pointer forward to remove first char
                    tmpfloat = hextofloat(separator)*0.10; //convert from hex to float and divide by 10 - using multiply as it is faster than divide                  
                    if (enableDebug == true ){
                      client.publish(debugTopic,"Positive Temp",true);
                    }
                  }
                  if (tmpfloat < TempMax) //test if we are inside the maximum test point - if not, assume spurious data
                    {root.set<float>( NamePart ,tmpfloat ); // passed spurious test - add data to root
                    } } /// end of TEMP block
                else if (NamePart == "HUM") { // test if it is HUM, which is int
                  //if (strcmp(RFName,"DKW2012") == 0 ) { // digitech weather station - assume it is a hex humidity, not straight int
                  //  tmpint = hextoint(separator);}
                  //else {
                    tmpint = atoi(separator);
                  //} // end of setting tmpint to the value we want to use & test
                  if (tmpint > 0 and tmpint < HumMax) //test if we are inside the maximum test point - if not, assume spurious data
                      {root.set<int>( NamePart ,tmpint); } // passed the test - add the data to rot, otherwise it will not be added as spurious
                    }  // end of HUM block                
                else if (NamePart == "RAIN")  // test if it is RAIN, which is HEX
                    {root.set<float>( NamePart ,hextofloat(separator)*0.10 );} //  - add data to root
                else if (NamePart == "WINSP")  // test if it is WINSP, which is HEX
                    {root.set<float>( NamePart ,hextofloat(separator)*0.10 );} //  - add data to root
                else if (NamePart == "WINGS")  // test if it is WINGS, which is HEX
                    {root.set<float>( NamePart ,hextofloat(separator)*0.10 );} //  - add data to root
                else if (NamePart == "WINDIR")  // test if it is WINDIR, which is 0-15 representing the wind angle / 22.5 - convert to compas text
                    {root[NamePart] = CompassDirTable[atoi(separator)] ;} 
                else // check if an int, add as int, else add as text
                  if (atoi(separator) == 0)  // not an integer
                    {root[NamePart] = separator ;} // do normal string add
                  else 
                    {root.set<int>( NamePart , atoi(separator) ); }// do int add
              } 
                
            
            // Find the next command in input string
            command = strtok(NULL, ";");
            }

//
 //       strtokIndx2 = strtok(RFDataTemp,";");      // get the first part - the string
        }
    else if (strcmp(messageFromPC,"10") == 0 ) { // 10 means a message command to RFLINK
      Serial.println("doing the else if - a 10 code "); 
      strcpy(RFData , strtokIndx ); // copy all of it to RFData 
      strcpy( RFName , "unknown" );
      strcpy( RFID , "10");
      root["raw"] = receivedChars; // copy the raw data to the json in case we need to debug
    }
    else if (strcmp(messageFromPC,"11") == 0 ) { // 11 means a message recieved to rflink to echo back
      Serial.println("doing the else if - a 11 code "); 
      strcpy(RFData , strtokIndx ); // copy all of it to RFData 
      strcpy( RFName , "unknown" );
      strcpy( RFID , "11");
      root["raw"] = receivedChars; // copy the raw data to the json in case we need to debug
    }
    else { // not a 20 code- something else
      Serial.println("doing the else - not a 20 code "); 
      strcpy(RFData , strtokIndx ); // copy all of it to RFData 
      strcpy( RFName , "unknown" );
      strcpy( RFID , "");
      root["raw"] = receivedChars; // copy the raw data to the json in case we need to debug
    }
//    
    // build the topic ("RF/" + RFName + "-" + RFID );

    MQTTTopic = "RF/" ;
    MQTTTopic += String(RFName); 
    MQTTTopic += "-" ;
    MQTTTopic += String(RFID) ;
    size_t lenM = MQTTTopic.length(); // returns length of the json     
    size_t sizeM = lenM + 1;

    char MQTTTopicConst[lenM];
    MQTTTopic.toCharArray(MQTTTopicConst,sizeM) ;
    
    // place the json data into variable 'json' for publishing over MQTT
    size_t len = root.measureLength(); // returns length of the json 
    size_t size = len+1;
    char json[size];
    root.printTo(json,size);

    Serial.print(MQTTTopicConst);
    Serial.print("   ");
    Serial.println(json);
    client.publish(MQTTTopicConst  , json , willRetain);

}

//============

void showParsedData() {
    Serial.print("Got something : ");
    Serial.println(receivedChars);
    // mqtt structure
/*
    Serial.print("MQTT: /RF/");    
    Serial.print(RFName);
    Serial.print("-");
    Serial.print(RFID);
    Serial.println("/");
    Serial.print("{");
    Serial.print(RFData);
    Serial.println("} @@");
*/

    }


//============

    void loop() {
    recvWithStartEndMarkers();
    if (newData == true) {
        strcpy(tempChars, receivedChars);
            // this temporary copy is necessary to protect the original data
            //   because strtok() used in parseData() replaces the commas with \0
        if (enableDebug == true ){
            client.publish(debugTopic,receivedChars,true);
}
        parseData();
        if (testmode == true) {showParsedData();}// we are in live mode and will parse from swSer rather than serial
        newData = false;
    }
    if (!client.connected() and enableMQTT ) {
      reconnect();
    }
    client.loop();

    // listen for OTA reprogramming
    ArduinoOTA.handle(); 
}

