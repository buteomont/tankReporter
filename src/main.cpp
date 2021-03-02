#include <Arduino.h>
#include <PubSubClient.h> 
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include "tankReporter.h"

#define VERSION "21.1.28.1"  //remember to update this after every change! YY.MM.DD.REV

//PubSubClient callback function header.  This must appear before the PubSubClient constructor.
void incomingMqttHandler(char* reqTopic, byte* payload, unsigned int length);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// These are the settings that get stored in EEPROM.  They are all in one struct which
// makes it easier to store and retrieve from EEPROM.
typedef struct 
  {
  unsigned int validConfig=0; 
  char ssid[SSID_SIZE] = "";
  char wifiPassword[PASSWORD_SIZE] = "";
  char mqttBrokerAddress[ADDRESS_SIZE]=""; //default
  int mqttBrokerPort=1883;
  char mqttUsername[USERNAME_SIZE]="";
  char mqttPassword[PASSWORD_SIZE]="";
  char mqttTopicRoot[MQTT_TOPIC_SIZE]="";
  char mqttClientId[MQTT_CLIENTID_SIZE]=""; //will be the same across reboots
  bool debug=false;
  unsigned long reportPeriod=0;
  char staticIP[ADDRESS_SIZE]="";
  char netmask[ADDRESS_SIZE]="";
  char gateway[ADDRESS_SIZE]="";
  char dns[ADDRESS_SIZE]="";
  } conf;

conf settings; //all settings in one struct makes it easier to store in EEPROM
boolean settingsAreValid=false;

String commandString = "";     // a String to hold incoming commands from serial
bool commandComplete = false;  // goes true when enter is pressed

char* clientId = settings.mqttClientId;
boolean wifiConnecting=false;
boolean ssidAvailable=false;
int connectTryCount=0;

unsigned long nextReport=0;
unsigned long nextFlash=0;
boolean warningLedOn=false;

int lastReading=0;

IPAddress staticIP;
IPAddress subnet;
IPAddress gateway;
IPAddress dns;

void flashWarning(boolean val)
  {
  // Serial.print("val is ");
  // Serial.println(val);
  if (millis()>=nextFlash && val==DRY)
    {
    if (warningLedOn) //make it yellow
      {
      analogWrite(OK_LED_PORT_GREEN,DRY_GREEN_BRIGHTNESS);
      analogWrite(WARNING_LED_PORT_RED,DRY_RED_BRIGHTNESS);
      }
    else
      {
      digitalWrite(OK_LED_PORT_GREEN,LED_OFF);
      digitalWrite(WARNING_LED_PORT_RED,LED_OFF);
      }
    warningLedOn=!warningLedOn;
    nextFlash=millis()+WARNING_LED_FLASH_RATE*1000;
    }
  else if (val==WET)
    {
//    Serial.println("WET");
    digitalWrite(WARNING_LED_PORT_RED,LED_OFF);
    analogWrite(OK_LED_PORT_GREEN,WET_GREEN_BRIGHTNESS); //full on is too bright
    }
  }

//Take a measurement
void readSensor()
  {
  int val=digitalRead(SENSOR_PORT);
  flashWarning((boolean)val);
  lastReading=val;
  }

void showSettings()
  {
  try
    {  
    Serial.print("broker=<MQTT broker host name or address> (");
    Serial.print(settings.mqttBrokerAddress);
    Serial.println(")");
    Serial.print("port=<port number>   (");
    Serial.print(settings.mqttBrokerPort);
    Serial.println(")");
    Serial.print("topicroot=<topic root> (");
    Serial.print(settings.mqttTopicRoot);
    Serial.println(")");  
    Serial.print("user=<mqtt user> (");
    Serial.print(settings.mqttUsername);
    Serial.println(")");
    Serial.print("pass=<mqtt password> (");
    Serial.print(settings.mqttPassword);
    Serial.println(")");
    Serial.print("ssid=<wifi ssid> (");
    Serial.print(settings.ssid);
    Serial.println(")");
    Serial.print("wifipass=<wifi password> (");
    Serial.print(settings.wifiPassword);
    Serial.println(")");
    Serial.print("debug=<1|0> (");
    Serial.print(settings.debug);
    Serial.println(")");
    Serial.print("MQTT Client ID is ");
    Serial.println(settings.mqttClientId);
    Serial.print("reportperiod=<seconds between reports> (");
    Serial.print(settings.reportPeriod);
    Serial.println(")");
    Serial.print("staticaddress=<IP address> (");
    Serial.print(settings.staticIP);
    Serial.println(")");
    Serial.print("netmask=<network IP mask> (");
    Serial.print(settings.netmask);
    Serial.println(")");
    Serial.print("gateway=<gateway IP address> (");
    Serial.print(settings.gateway);
    Serial.println(")");
    Serial.print("dns=<DNS IP address> (");
    Serial.print(settings.dns);
    Serial.println(")");
    Serial.print("Settings are");
    Serial.print(settingsAreValid?"":" not");
    Serial.println(" valid.");
    Serial.println("\n*** Use \"factorydefaults=yes\" to reset all settings ***\n");
    }
  catch(const std::exception& e)
    {
    Serial.println("******************* ERROR ************");
    Serial.println(e.what());
    }

  }

void showSub(char* topic)
  {
  if (settings.debug)
    {
    Serial.print("++++++Subscribing to ");
    Serial.print(topic);
    Serial.print(":\t");
    Serial.println();
    }
  }

/*
 * Save the settings to EEPROM. Set the valid flag if everything is filled in.
 */
boolean saveSettings()
  {
  if (strlen(settings.ssid)>0 &&
    strlen(settings.wifiPassword)>0 &&
    strlen(settings.mqttBrokerAddress)>0 &&
    settings.mqttBrokerPort!=0 &&
    strlen(settings.mqttTopicRoot)>0 &&
    strlen(settings.mqttClientId)>0 &&
    settings.reportPeriod > 0 &&
      (strlen(settings.staticIP)==0 || //if staticIP set then all network stuff
        (strlen(settings.netmask)>0 && //except DNS must be too
        strlen(settings.gateway)>0))
    )
    {
    Serial.println("Settings deemed complete");
    settings.validConfig=VALID_SETTINGS_FLAG;
    settingsAreValid=true;
    }
  else
    {
    Serial.println("Settings still incomplete");
    settings.validConfig=0;
    settingsAreValid=false;
    }
    
  //The mqttClientId is not set by the user, but we need to make sure it's set  
  if (strlen(settings.mqttClientId)==0)
    {
    strcpy(settings.mqttClientId,strcat((char*)MQTT_CLIENT_ID_ROOT,String(random(0xffff), HEX).c_str()));
    }
    
    
  EEPROM.put(0,settings);
  return EEPROM.commit();
  }

void initializeSettings()
  {
  settings.validConfig=0; 
  strcpy(settings.ssid,"");
  strcpy(settings.wifiPassword,"");
  strcpy(settings.mqttBrokerAddress,""); //default
  settings.mqttBrokerPort=1883;
  strcpy(settings.mqttUsername,"");
  strcpy(settings.mqttPassword,"");
  strcpy(settings.mqttTopicRoot,"");
  strcpy(settings.mqttClientId,strcat((char*)MQTT_CLIENT_ID_ROOT,String(random(0xffff), HEX).c_str()));
  settings.reportPeriod=0;
  strcpy(settings.staticIP,"");
  strcpy(settings.netmask,"");
  strcpy(settings.gateway,"");
  strcpy(settings.dns,"");
  }

bool processCommand(String cmd)
  {
  const char *str=cmd.c_str();
  char *val=NULL;
  char *nme=strtok((char *)str,"=");
  if (nme!=NULL)
    val=strtok(NULL,"=");

  //Get rid of the carriage return
  if (val!=NULL && strlen(val)>0 && val[strlen(val)-1]==13)
    val[strlen(val)-1]=0; 

  if (nme==NULL || val==NULL || strlen(nme)==0 || strlen(val)==0)
    {
    showSettings();
    return false;   //bad or missing command
    }
  if (strcmp(val,"null")==0) //they want to reset a value
    {
    strcpy(val,"");
    }
  if (strcmp(nme,"broker")==0)
    {
    strcpy(settings.mqttBrokerAddress,val);
    saveSettings();
    }
  else if (strcmp(nme,"port")==0)
    {
    settings.mqttBrokerPort=atoi(val);
    saveSettings();
    }
  else if (strcmp(nme,"topicroot")==0)
    {
    strcpy(settings.mqttTopicRoot,val);
    if (settings.mqttTopicRoot[strlen(settings.mqttTopicRoot)-1]!='/')
      strcat(settings.mqttTopicRoot,"/");
    saveSettings();
    }
  else if (strcmp(nme,"user")==0)
    {
    strcpy(settings.mqttUsername,val);
    saveSettings();
    }
  else if (strcmp(nme,"pass")==0)
    {
    strcpy(settings.mqttPassword,val);
    saveSettings();
    }
  else if (strcmp(nme,"ssid")==0)
    {
    strcpy(settings.ssid,val);
    saveSettings();
    }
  else if (strcmp(nme,"wifipass")==0)
    {
    strcpy(settings.wifiPassword,val);
    saveSettings();
    }
  else if (strcmp(nme,"debug")==0)
    {
    settings.debug=atoi(val)==1?true:false;
    saveSettings();
    }
  else if (strcmp(nme,"reportperiod")==0)
    {
    settings.reportPeriod=atol(val);
    nextReport=millis()+settings.reportPeriod;
    saveSettings();
    }
  else if (strcmp(nme,"staticaddress")==0)
    {
    strcpy(settings.staticIP,val);
    saveSettings();
    }
  else if (strcmp(nme,"netmask")==0)
    {
    strcpy(settings.netmask,val);
    saveSettings();
    }
  else if (strcmp(nme,"gateway")==0)
    {
    strcpy(settings.gateway,val);
    saveSettings();
    }
  else if (strcmp(nme,"dns")==0)
    {
    strcpy(settings.dns,val);
    saveSettings();
    }
  else if ((strcmp(nme,"factorydefaults")==0) && (strcmp(val,"yes")==0)) //reset all eeprom settings
    {
    Serial.println("\n*********************** Resetting EEPROM Values ************************");
    initializeSettings();
    saveSettings();
    delay(2000);
    ESP.restart();
    }
  else //invalid command
    {
    showSettings();
    return false;
    }
  return true;
  }
  
/*
  SerialEvent occurs whenever a new data comes in the hardware serial RX. This
  routine is run between each time loop() runs, so using delay inside loop can
  delay response. Multiple bytes of data may be available.
*/
void serialEvent() 
  {
  while (Serial.available()) 
    {
    // get the new byte
    char inChar = (char)Serial.read();

    // if the incoming character is a newline, set a flag so the main loop can
    // do something about it 
    if (inChar == '\n') 
      {
      commandComplete = true;
      }
    else
      {
      // add it to the inputString 
      commandString += inChar;
      }
    }
  }

/*
 * Check for configuration input via the serial port.  Return a null string 
 * if no input is available or return the complete line otherwise.
 */
String getConfigCommand()
  {
  if (commandComplete) 
    {
    Serial.println(commandString);
    String newCommand=commandString;

    commandString = "";
    commandComplete = false;
    return newCommand;
    }
  else return "";
  }

void checkForCommand()
  {
  if (Serial.available())
    {
    serialEvent();
    String cmd=getConfigCommand();
    if (cmd.length()>0)
      {
      processCommand(cmd);
      }
    }
  }

  /*
 * Reconnect to the MQTT broker
 */
void mqttReconnect() 
  {
  if (!settingsAreValid || WiFi.status() != WL_CONNECTED) //don't bother
    {
    return;
    }

  mqttClient.loop(); //This has to happen every so often or we can't receive messages
    
  if (!mqttClient.connected()) // only if we aren't already connected
    {
    if (settings.debug)
      {
      Serial.print("\nAttempting MQTT connection...");
      }
    
    // Attempt to connect
    if (mqttClient.connect(settings.mqttClientId,settings.mqttUsername,settings.mqttPassword))
      {
      if (settings.debug)
        {
        Serial.println("connected to MQTT broker.");
        }
      //subscribe to the incoming message topics
      char topic[MQTT_TOPIC_SIZE];
      strcpy(topic,settings.mqttTopicRoot);
      strcat(topic,MQTT_TOPIC_COMMAND_REQUEST);
      int subok=mqttClient.subscribe(topic);
      if (subok!=1)
        {
        Serial.print("Unable to subscribe to ");
        Serial.println(topic);
        Serial.print("Return code: ");
        Serial.println(subok);
        }
      else
        showSub(topic);
      }
    else 
      {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
//      Serial.println("Will try again in a second");
      
      // Wait a second before retrying
      // In the meantime check for input in case something needs to be changed to make it work
      checkForCommand(); 
      
//      delay(1000);
      }
    }
  }

boolean publish(char* topic, char* reading, bool retain)
  {
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(reading);
  if (mqttClient.connected())
    return mqttClient.publish(topic,reading,retain);
  else
    return true;
  }

/************************
 * Do the MQTT thing
 ************************/
void report()
  {  
  char topic[MQTT_TOPIC_SIZE];
  char value[18];
  boolean success=false;

  //publish the last reading value
  strcpy(topic,settings.mqttTopicRoot);
  strcat(topic,MQTT_TOPIC_READING);
  sprintf(value,"%d",lastReading); 
  success=publish(topic,value,true); //retain
  if (!success)
    Serial.println("************ Failed publishing sensor reading!");

  //publish the fuel reading
  strcpy(topic,settings.mqttTopicRoot);
  strcat(topic,MQTT_TOPIC_LEVEL);
  sprintf(value,"%s",lastReading?MQTT_PAYLOAD_SENSOR_WET:MQTT_PAYLOAD_SENSOR_DRY); //item within range window
  success=publish(topic,value,true); //retain
  if (!success)
    Serial.println("************ Failed publishing moisture value!");
  }

  
/*
*  Initialize the settings from eeprom and determine if they are valid
*/
void loadSettings()
  {
  EEPROM.get(0,settings);
  if (settings.validConfig==VALID_SETTINGS_FLAG)    //skip loading stuff if it's never been written
    {
    settingsAreValid=true;
    if (settings.debug)
      {
      Serial.println("Loaded configuration values from EEPROM");
      }
//    showSettings();
    }
  else
    {
    Serial.println("Skipping load from EEPROM, device not configured.");    
    settingsAreValid=false;
    }
  }

/**
 * Handler for incoming MQTT messages.  The payload is the command to perform. 
 * The MQTT message topic sent is the topic root plus the command.
 * Implemented commands are: 
 * MQTT_PAYLOAD_SETTINGS_COMMAND: sends a JSON payload of all user-specified settings
 * MQTT_PAYLOAD_REBOOT_COMMAND: Reboot the controller
 * MQTT_PAYLOAD_VERSION_COMMAND Show the version number
 * MQTT_PAYLOAD_STATUS_COMMAND Show the most recent flow values
 * 
 * Note that unless sleeptime is zero, any MQTT command must be sent in the short time
 * between connecting to the MQTT server and going to sleep.  In this case it is best
 * to send the command with the "retain" flag on. Be sure to remove the retained message
 * after it has been received by sending a new empty message with the retained flag set.
 */
void incomingMqttHandler(char* reqTopic, byte* payload, unsigned int length) 
  {
  if (settings.debug)
    {
    Serial.print("*************************** Received topic ");
    Serial.println(reqTopic);
    }
  payload[length]='\0'; //this should have been done in the caller code, shouldn't have to do it here
  boolean rebootScheduled=false; //so we can reboot after sending the reboot response
  char charbuf[100];
  sprintf(charbuf,"%s",reqTopic);
  char* response;
  char topic[MQTT_TOPIC_SIZE];

  //General command?
  strcpy(topic,settings.mqttTopicRoot);
  strcat(topic,MQTT_TOPIC_COMMAND_REQUEST);
  if (strcmp(charbuf,topic)==0) //then we have received a command
    {
    sprintf(charbuf,"%s",payload);
  
    //if the command is MQTT_PAYLOAD_SETTINGS_COMMAND, send all of the settings
    if (strcmp(charbuf,MQTT_PAYLOAD_SETTINGS_COMMAND)==0)
      {
      char tempbuf[35]; //for converting numbers to strings
      char jsonStatus[JSON_STATUS_SIZE];
      
      strcpy(jsonStatus,"{");
      strcat(jsonStatus,"\"broker\":\"");
      strcat(jsonStatus,settings.mqttBrokerAddress);
      strcat(jsonStatus,"\", \"port\":");
      sprintf(tempbuf,"%d",settings.mqttBrokerPort);
      strcat(jsonStatus,tempbuf);
      strcat(jsonStatus,", \"topicroot\":\"");
      strcat(jsonStatus,settings.mqttTopicRoot);
      strcat(jsonStatus,"\", \"user\":\"");
      strcat(jsonStatus,settings.mqttUsername);
      strcat(jsonStatus,"\", \"pass\":\"");
      strcat(jsonStatus,settings.mqttPassword);
      strcat(jsonStatus,"\", \"ssid\":\"");
      strcat(jsonStatus,settings.ssid);
      strcat(jsonStatus,"\", \"wifipass\":\"");
      strcat(jsonStatus,settings.wifiPassword);
      strcat(jsonStatus,"\", \"mqttClientId\":\"");
      strcat(jsonStatus,settings.mqttClientId);
      strcat(jsonStatus,"\", \"reportPeriod\":\"");
      sprintf(tempbuf,"%lu",settings.reportPeriod);
      strcat(jsonStatus,tempbuf);
      strcat(jsonStatus,"\", \"staticaddress\":\"");
      strcat(jsonStatus,settings.staticIP);
      strcat(jsonStatus,"\", \"netmask\":\"");
      strcat(jsonStatus,settings.netmask);
      strcat(jsonStatus,"\", \"gateway\":\"");
      strcat(jsonStatus,settings.gateway);
      strcat(jsonStatus,"\", \"dns\":\"");
      strcat(jsonStatus,settings.dns);
      strcat(jsonStatus,"\", \"debug\":\"");
      strcat(jsonStatus,settings.debug?"true":"false");
      strcat(jsonStatus,"\", \"localIP\":\"");
      strcat(jsonStatus,WiFi.localIP().toString().c_str());

      strcat(jsonStatus,"\"}");
      response=jsonStatus;
      }
    else if (strcmp(charbuf,MQTT_PAYLOAD_VERSION_COMMAND)==0) //show the version number
      {
      char tmp[15];
      strcpy(tmp,VERSION);
      response=tmp;
      }
    else if (strcmp(charbuf,MQTT_PAYLOAD_STATUS_COMMAND)==0) //show the latest value
      {
      report();
      
      char tmp[25];
      strcpy(tmp,"Status report complete");
      response=tmp;
      }
    else if (strcmp(charbuf,MQTT_PAYLOAD_REBOOT_COMMAND)==0) //reboot the controller
      {
      char tmp[10];
      strcpy(tmp,"REBOOTING");
      response=tmp;
      rebootScheduled=true;
      }
    else if (processCommand(charbuf))
      {
      char tmp[3];
      strcpy(tmp,"OK");
      response=tmp;
      }
    else
      {
      char badCmd[18];
      strcpy(badCmd,"(empty)");
      response=badCmd;
      }
      
    char topic[MQTT_TOPIC_SIZE];
    strcpy(topic,settings.mqttTopicRoot);
    strcat(topic,charbuf); //the incoming command becomes the topic suffix
  
    if (!publish(topic,response,false)) //do not retain
      {
      int code=mqttClient.state();
      Serial.print("************ Failure ");
      Serial.print(code);
      Serial.println(" when publishing command response!");
      }

    delay(2000); //give publish time to complete
    }

  if (rebootScheduled)
    {
    ESP.restart();
    }
  }

/*
 * Save the settings object to EEPROM and publish a "show settings" command.
 */
void saveAndShow()
  {
  saveSettings();
  char topic[MQTT_TOPIC_SIZE];
  strcpy(topic,settings.mqttTopicRoot);
  strcat(topic,MQTT_TOPIC_COMMAND_REQUEST); //Send ourself the command to display settings

  if (!publish(topic,(char*)MQTT_PAYLOAD_SETTINGS_COMMAND,false)) //do not retain
    Serial.println("************ Failure when publishing show settings response!");
  }

boolean inRange()
  {
  if (ssidAvailable)
    return(true); //already found it

  boolean avail=false;  //temporary found flag

  if (settings.debug)
    Serial.println("scan start");
  
  int n = WiFi.scanNetworks();
  if (settings.debug)
    {
    Serial.println("scan done");
    if (n == 0)
      Serial.println("no networks found");
    else
      Serial.print("Found ");
      Serial.print(n);
      Serial.println(" WiFi access points:\n");
    }
  if (n>0)
    {
    for (int i=0;i<n;++i)
      {
      if(WiFi.SSID(i) == settings.ssid)
        {
        avail=true;  //remember it, but don't quit looking
        if (settings.debug)
          {
          Serial.print("Found target SSID: ");
          }
        }
      if (settings.debug)
        {
        Serial.println(WiFi.SSID(i));// Print SSID for each network found
        }
      }
    if (settings.debug)
      {
      Serial.println();
      }
    }
  ssidAvailable=avail?true:false;  //advertise if we found it or not
  return avail;
  }

void connectToWiFi()
  {
    if (wifiConnecting && WiFi.status() == WL_CONNECTED)
      {
      wifiConnecting=false;
      if (settings.debug)
        {
        Serial.print("Connected to WiFi with address ");
        Serial.println(WiFi.localIP());
        }
      }

  if (WiFi.status()==WL_CONNECTED || !inRange())
    return;  //don't bother

  if (connectTryCount++ >= 100)  //give up after a while
    {
    ssidAvailable=false;
    connectTryCount=0;

    if (settings.debug)
      Serial.println("Timeout trying to connect to wifi.");
    
    return;
    }
  else
    {
    delay(500); //delay only during connect process
    }
  // ********************* attempt to connect to Wifi network

  if (!wifiConnecting)
    {
    if (settings.debug)
      {
      Serial.print("Attempting to connect to WPA SSID \"");
      Serial.print(settings.ssid);
      Serial.print("\" using ");
      Serial.println(settings.staticIP?settings.staticIP:"DHCP");
      }
    WiFi.hostname(MY_HOSTNAME);

    //If a static IP address is specified then use it
    if (staticIP && gateway && subnet && dns)
      {
      WiFi.disconnect();  //Prevent connecting to wifi based on previous configuration
      Serial.print("...connecting with static address ");
      Serial.println(settings.staticIP);
      WiFi.config(staticIP, gateway, subnet, dns);
      }
    else if (staticIP && gateway && subnet)
      {
      WiFi.disconnect();  //Prevent connecting to wifi based on previous configuration
      Serial.print("...connecting (no DNS) with static address ");
      Serial.println(settings.staticIP);
      WiFi.config(staticIP, gateway, subnet);
      }

    WiFi.begin(settings.ssid, settings.wifiPassword);
    WiFi.mode(WIFI_STA); //station mode, we are only a client in the wifi world
    wifiConnecting=true;
    }
  if (WiFi.status() != WL_CONNECTED && wifiConnecting) 
    {
    // not yet connected
    if (settings.debug)
      {
      Serial.print(".");
      }
//    checkForCommand(); // Check for input in case something needs to be changed to work
    }
  
  
  // ********************* Initialize the MQTT connection
  mqttClient.setBufferSize(JSON_STATUS_SIZE);
  mqttClient.setServer(settings.mqttBrokerAddress, settings.mqttBrokerPort);
  mqttClient.setCallback(incomingMqttHandler);
//    mqttReconnect();  // connect to the MQTT broker

  }

void setup() 
  {
  pinMode(SENSOR_PORT,INPUT_PULLUP); //The liquid level sensor has an open collector output
  pinMode(WIFI_LED_PORT,OUTPUT);// The blue light on the board shows wifi activity
  digitalWrite(WIFI_LED_PORT,LED_OFF);// Turn it off
  pinMode(WARNING_LED_PORT_RED,OUTPUT);// The yellow light on the board shows low tank
  digitalWrite(WARNING_LED_PORT_RED,LED_OFF);// Turn it off
  pinMode(OK_LED_PORT_GREEN,OUTPUT);// The green light on the board shows it's working
  digitalWrite(OK_LED_PORT_GREEN,LED_OFF);// Turn it off

  Serial.begin(115200);
  Serial.setTimeout(10000);
  Serial.println();
  
  while (!Serial); // wait here for serial port to connect.

  EEPROM.begin(sizeof(settings)); //fire up the eeprom section of flash
  if (settings.debug)
    {
    Serial.print("Settings object size=");
    Serial.println(sizeof(settings));
    }
    
  commandString.reserve(200); // reserve 200 bytes of serial buffer space for incoming command string

  loadSettings(); //set the values from eeprom
  if (settings.mqttBrokerPort < 0) //then this must be the first powerup
    {
    Serial.println("\n*********************** Resetting All EEPROM Values ************************");
    initializeSettings();
    saveSettings();
    delay(2000);
    ESP.restart();
    }

  if (settingsAreValid)
    {
    showSettings();
    }

  if (sizeof(settings.staticIP)>0)
    staticIP.fromString(settings.staticIP);
  if (sizeof(settings.netmask)>0)
    subnet.fromString(settings.netmask);
  if (sizeof(settings.gateway)>0)
    gateway.fromString(settings.gateway);
  if (sizeof(settings.dns)>0)
    dns.fromString(settings.dns);

  ArduinoOTA.onStart([]() 
    {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) 
      {
      type = "sketch";
      }
    else  // U_FS
      {
      type = "filesystem";
      }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
    });
  ArduinoOTA.onEnd([]() 
    {
    Serial.println("\nEnd");
    });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) 
    {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
  ArduinoOTA.onError([](ota_error_t error) 
    {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) 
      {
      Serial.println("Auth Failed");
      } 
    else if (error == OTA_BEGIN_ERROR) 
      {
      Serial.println("Begin Failed");
      } 
    else if (error == OTA_CONNECT_ERROR) 
      {
      Serial.println("Connect Failed");
      } 
    else if (error == OTA_RECEIVE_ERROR) 
      {
      Serial.println("Receive Failed");
      }
    else if (error == OTA_END_ERROR) 
      {
      Serial.println("End Failed");
      }
    });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  }


void loop() 
  {
  checkForCommand(); // Check for serial input in case something needs to be changed
  readSensor();      // Take a reading

  if (WiFi.status()==WL_CONNECTED)
    {
    digitalWrite(WIFI_LED_PORT,LED_ON);
    ArduinoOTA.handle();// Check for new code
    }
  else
    digitalWrite(WIFI_LED_PORT,LED_OFF);
  

  if (settingsAreValid) 
    {
    if (millis()>=nextReport)
      {
      connectToWiFi(); //try to connect if available

      // may need to reconnect to the MQTT broker. This is true even if the report is 
      // already sent, because a MQTT command may come in
      mqttReconnect();  
      report();    
      nextReport=millis()+settings.reportPeriod*1000;
      }
    } 
  }