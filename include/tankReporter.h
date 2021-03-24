// Pins

#define LED_ON LOW
#define LED_OFF HIGH

#define SENSOR_PORT D1
#define WARNING_LED_PORT_RED D2
#define OK_LED_PORT_GREEN D3
#define DRY LED_ON
#define WET LED_OFF
#define DRY_RED_BRIGHTNESS 48
#define DRY_GREEN_BRIGHTNESS 128
#define WET_GREEN_BRIGHTNESS 200 //higher number is less bright
#define HYSTERESIS_DELAY 2000 //milliseconds
#define WIFI_LED_PORT LED_BUILTIN
#define WARNING_LED_FLASH_RATE 1 //seconds
#define VALID_SETTINGS_FLAG 0xDAB0
#define SSID_SIZE 100
#define PASSWORD_SIZE 50
#define ADDRESS_SIZE 30
#define USERNAME_SIZE 50
#define MY_HOSTNAME "GolfCart"
#define MQTT_CLIENTID_SIZE 25
#define MQTT_TOPIC_SIZE 150
#define MQTT_TOPIC_LEVEL "level"
#define MQTT_TOPIC_READING "value"
#define MQTT_TOPIC_PERIOD "period"
#define MQTT_CLIENT_ID_ROOT "tankReporter"
#define MQTT_TOPIC_COMMAND_REQUEST "command"
#define MQTT_PAYLOAD_SENSOR_WET "wet"
#define MQTT_PAYLOAD_SENSOR_DRY "dry"
#define MQTT_PAYLOAD_SETTINGS_COMMAND "settings" //show all user accessable settings
#define MQTT_PAYLOAD_RESET_PULSE_COMMAND "resetPulseCounter" //reset the pulse counter to zero
#define MQTT_PAYLOAD_REBOOT_COMMAND "reboot" //reboot the controller
#define MQTT_PAYLOAD_VERSION_COMMAND "version" //show the version number
#define MQTT_PAYLOAD_STATUS_COMMAND "status" //show the most recent flow values
#define JSON_STATUS_SIZE 450 //Keep an eye on this if status items are added
#define PUBLISH_DELAY 400 //milliseconds to wait after publishing to MQTT to allow transaction to finish

// Error codes copied from the MQTT library
// #define MQTT_CONNECTION_REFUSED            -2
// #define MQTT_CONNECTION_TIMEOUT            -1
// #define MQTT_SUCCESS                        0
// #define MQTT_UNACCEPTABLE_PROTOCOL_VERSION  1
// #define MQTT_IDENTIFIER_REJECTED            2
// #define MQTT_SERVER_UNAVAILABLE             3
// #define MQTT_BAD_USER_NAME_OR_PASSWORD      4
// #define MQTT_NOT_AUTHORIZED                 5

//WiFi status codes
//0 : WL_IDLE_STATUS when Wi-Fi is in process of changing between statuses
//1 : WL_NO_SSID_AVAILin case configured SSID cannot be reached
//3 : WL_CONNECTED after successful connection is established
//4 : WL_CONNECT_FAILED if password is incorrect
//6 : WL_DISCONNECTED if module is not configured in station mode

