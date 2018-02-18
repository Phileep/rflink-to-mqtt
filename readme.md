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

Payload example: {"raw":"20;B3;DKW2012;ID=004c;TEMP=00eb;HUM=3f;WINSP=0000;WINGS=0000;RAIN=0003;WINDIR=0009;\r","TEMP":23.50,"HUM":3,"WINSP":"0000","WINGS":"0000","RAIN":3,"WINDIR":"SSW"}
see RFLink documentation for more details: http://www.nemcon.nl/blog2/protref

Sending commands to the RFLink example: (how to send codes to the RFLink)

Topic: RF/command

Payload: 10;Eurodomest;02d0f2;06;ALLON\n
    Note that the \n on the end is critical at the moment. Otherwise the ESP will either crash or the RFLink will ignore



I included the raw data in full for debugging, then the split out components from RFink
There is some conversions made to make the data more useable:
    Temp is converted to a float from hex
    Wind direction is converted to a compass heading
    etc - details in the parseData function in the .ino file 


Requirements:
Arduino mega 2560 and RF reciever and or transmitter (see RFLink for recommended devices and wiring)
ESP8266 device (I have used a node MCU V1.0)
an MQTT broker runnning. Tested with Mosquitto on a raspberry Pi 2

Optional: 
Somethig to read and react to the MQTT measurements
I am using Home Assistant, also running on the same Pi : https://home-assistant.io/

Note: If you want to use a username and password for your mqtt broker, Change line 205 in the RFLink_to_MQTT.ino to include username and password as per the PubSubClient API. see https://pubsubclient.knolleary.net/api.html#connect4

Setup:
1) Confirm RFLink working on its own through your USB port - then you know you are getting data recieved etc
    Collect some data from your RF433 devices on the system monitor screen and save them for step 2

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
