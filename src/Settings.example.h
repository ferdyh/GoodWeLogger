//Wifi SSID to connect to
#define WIFI_SSID "<your wifi ssid>"

//Passowrd for WIFI
#define WIFI_PASSWORD "<your wifi pwd>"

//set the mqqt host name or ip address to your mqqt host. Leave empty to disable mqtt.
#define MQTT_HOST_NAME  "192.168.2.2"

//mqtt port for the above host
#define MQTT_PORT       1883

//if authentication is enabled for mqtt, set the username below. Leave empty to disable authentication
#define MQTT_USER_NAME  "<mqtt user name>"

//password for above user
#define MQTT_PASSWORD   "<mqtt password>"

//update interval for fast changing values in milliseconds for mqtt
#define MQTT_QUICK_UPDATE_INTERVAL  10000

//update interval for slow changing values in milliseconds for mqtt
#define MQTT_REGULAR_UPDATE_INTERVAL  60000

//rs485 receive pin
#define RS485_RX D1

//rs485 transmit pin
#define RS485_TX D2

//Hostname to use on local network
#define WIFI_HOSTNAME "GoodWeLogger"

//NTP server addres
#define NTP_SERVER "pool.ntp.org"

//Wifi connection timeout. If Wifi connection is lost for this long the ESP is restarted
#define WIFI_CONNECT_TIMEOUT 30*1000 

//Inverter data reset after 11 minutes (inverter reconnect timeout is 10 minutes, 1 minute extra to avoid too quick reset) 
#define INVERTER_OFFLINE_RESET_VALUES_TIMEOUT 11*60*1000 

//Enable debbugging through serial or remote
#define DEBUGGING_ENABLED true

//Enable telnet/remote debugging?
#define REMOTE_DEBUGGING_ENABLED true