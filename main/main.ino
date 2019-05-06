#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Ticker.h>

//WiFi and MQTT connection settings
//const char* ssid = "KIT";
//const char* password = "shmu5422";

const char* ssid = "AndroidAP895A";
const char* password = "shmu5422";

const char* mqtt_server = "prometheus.lukasweiser.de";
const int mqtt_port = 1883;
//const char* mqtt_user = "lukas";
//const char* mqtt_pass = "HpFyqXV4rGHdPu6qNFoa";
const char* mqtt_client_name = "5C:CF:7F:78:76:A1"; 

// setup of wifi and mqtt client
WiFiClient espClient;
PubSubClient client(espClient);

//mqtt comm settings
long lastMsg = 0;
char msg[50];
int value = 0;



//Sensor and measure settings
const int SENSORS = 2;         //number of sensors attached
const int SAMPLES = 20;       //number of samples in each json
float SAMPLERATE = 2;  //Hertz
int data[SENSORS][SAMPLES];
String sensor_ids[SENSORS] = {"AA00", "AA01"};
String sensor_descriptions[SENSORS] = {"length measurement", "accelerometer"};
String sensor_units[SENSORS] = {"m", "mm/s^2"};
String sensor_group_ids[SENSORS] = {"", ""};
int data_capture_iteration = 0;
String timestamps[SENSORS];
bool update_timestamp = false;

//NTP and time settings
unsigned long t_stamp_millis_init;
unsigned long t_stamp_millis;
unsigned long t_stamp_seconds;
char t_stamp_millis_formatted[3];
WiFiUDP udp;
// NTPClient timeClient(ntpUDP);
NTPClient timeClient(udp, "europe.pool.ntp.org", 3600, 60000);

//MES connection infos
IPAddress remoteIp(192, 168, 43, 1);
unsigned short remotePort = 1337;
unsigned short localPort = 1337;

//JSON Settings
bool send_json = false;
char json_out[2048];

//Ticker
Ticker ticker;


//################### SETUP #####################################
void setup() {
  Serial.begin(115200);  
  
  setup_wifi();  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);


  //get inital ntp packet from server
  Serial.print("Update time by NTP");
  timeClient.begin();
  while (timeClient.getEpochTime() < 10000) {
    timeClient.update();
    Serial.print(".");
  }
  t_stamp_millis_init = millis();

  Serial.println("\r\nAttach iterrupt to ticker");
  ticker.attach(1 / SAMPLERATE, data_capture);

}


//################ LOOP #########################################
void loop() {
  
  // put your main code here, to run repeatedly:
   if (!client.connected()) {
    reconnect();
  }

  client.loop(); //must be called regulary to maintain connection to MQTT Server

  if (send_json == true) {    
    json_create_msg(WiFi.macAddress(), data, sensor_ids);
    mqttPublish("mes", json_out);
    send_json = false;
    data_capture_iteration = 0;
    
    for (int i = 0; i < SENSORS; i++) {
      for (int j = 0; j < SAMPLES; j++) {
        data[i][j] = 0;
      }
    }
  }
  
}




//################### FUNCTIONS ##########################

/**
 * 
 * 
 * 
 */
boolean mqttPublish(char* topic, char* msg) {
  
  client.publish(topic, msg);  
}



char* string2char(String command){
    if(command.length()!=0){
        char *p = const_cast<char*>(command.c_str());
        return p;
    }
}


/*
 * 
 * 
 * 
 * 
 */
void setup_wifi() {
  
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  
}


/*
 * 
 * 
 * 
 * 
 */
//callback function is called when subscribed topic provides a new message
void callback(char* topic, byte* payload, unsigned int length) {
  String payloadString = "";
  String topicString = String(topic);
  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    payloadString += (char)payload[i];
  }
  Serial.println();
}


/*
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */
void reconnect() {
  // Loop until we're reconnected
  
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    //if (client.connect(mqtt_client_name, mqtt_user, mqtt_pass)) {
    if (client.connect(mqtt_client_name)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("mes", mqtt_client_name);
      // ... and resubscribe
      client.subscribe("mes/rx");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  
}



/*
 * 
 * 
 * 
 * 
 * 
 * 
 */
String timestamp() {
  String ts;
  t_stamp_millis = ((millis() - t_stamp_millis_init) % 1000);
  sprintf(t_stamp_millis_formatted, "%03d", t_stamp_millis);
  ts = (timeClient.getFormattedTime() + ":" + t_stamp_millis_formatted);
  //Serial.println(ts);
  return ts;
}



/*
 * 
 * 
 * 
 * 
 * 
 * 
 */
void data_capture() {
  //Serial.println("capture " + String(data_capture_iteration + 1) + " of " + String(SAMPLES));
  //capture data from sensors
  for (int i = 0; i < SENSORS; i++) {

    //read sensor data here!
    data[i][data_capture_iteration] = (i + data_capture_iteration) * data_capture_iteration;
  }

  // set new timestamp if new measure set
  if (data_capture_iteration == 0) {
    String ts = timestamp();
    for (int i = 0; i < SENSORS; i++) {
      timestamps[i] = ts;
    }
  }
  //increase capture iteration
  data_capture_iteration++;

  //send json_send flag if data complete
  if (data_capture_iteration >= SAMPLES) {
    //send json flag
    send_json = true;


  }
}

/*
 * 
 * 
 * 
 * 
 * 
 */

void json_create_msg(String controller_id, int data[SENSORS][SAMPLES], String sensor_ids[SENSORS]) {
  DynamicJsonDocument doc(8192);
  //DynamicJsonDocument *doc = new DynamicJsonDocument(8192);
  JsonObject payload = doc.createNestedObject("payload");

  JsonArray payload_sensor = payload.createNestedArray("sensor");



  // for loop to create sensors

  for (int i = 0; i < SENSORS; i++) {
    JsonObject payload_sensor_0 = payload_sensor.createNestedObject();
    payload_sensor_0["id"] = sensor_ids[i];
    payload_sensor_0["samplerate"] = SAMPLERATE;
    payload_sensor_0["units"] = sensor_units[i];
    payload_sensor_0["description"] = sensor_descriptions[i];
    payload_sensor_0["group_id"] = sensor_group_ids[i];

    JsonObject payload_sensor_0_data = payload_sensor_0.createNestedObject("data");
    payload_sensor_0_data["timestamp"] = timestamps[i];

    JsonArray payload_sensor_0_data_values = payload_sensor_0_data.createNestedArray("values");
    for (int j = 0; j < SAMPLES; j++) {
      payload_sensor_0_data_values.add(data[i][j]);
    }
  }


  JsonObject meta = doc.createNestedObject("meta");
  JsonObject meta_controller = meta.createNestedObject("controller");
  meta_controller["id"] = controller_id;

  //serializeJson(doc, Serial);
  //Serial.println("");
  udp.beginPacket(remoteIp, remotePort);
  serializeJson(doc, udp);
  udp.println();
  udp.endPacket();
  
  serializeJson(doc, json_out);
  Serial.println(json_out);
  
}
