#include<WiFi.h>
#include<PubSubClient.h>
#include<Wire.h>
#include<SPI.h>
#include<Adafruit_BMP280.h>
#include<DHT.h>
#include<NTPClient.h>
#include<WiFiUdp.h>
#include<ArduinoJson.h>
#include<Ticker.h>
#include<math.h>

/*The following part is default config in nodemcu*/

/*SPI pins definition of bmp280, data type is based on the interface of function in <Adafruit_BMP280.h>*/
int8_t BMP280_SCK=14;
int8_t BMP280_MISO=12;
int8_t BMP280_MOSI=13;
int8_t BMP280_CS=27;
/*I2C pins definition of bmp280, data type is based on the interface of function <wire.h>*/
int BMP280_SDA=21;
int BMP280_SCL=22;
/*Single wire transmission pin of DHT11, data is type are defined by the interface of library file*/
uint8_t SINGLE_WIRE_PIN=2;
/*Define digital pin of led, data type is based on the interface of digitalRead()*/
uint8_t DIGITAL_PIN=5;
/*Analog pin*/
int ANALOG_PIN=34;
/*General switch of sample, 1 means on, 0 means off*/
int SAMPLE_SWITCH=0;
/*Actual amount of samples in each json, can be defined by user through nodered*/
int SAMPLES = 20;
/*Actual amount of sensors, can be defined by user through nodered,has to be adjusted to the real amount,
  because it will be used for instance in void send_config(char* topic) to generate a config in json form, very important!!!*/
int SENSORS=5;   
/*Sample rate defined by user,unit Hz*/
float SAMPLERATE=2;
/*Max allowed amount of sensors attached and samples in each json, these two parameters are used to create a 2d array to store sampled data*/
const int MAX_SENSORS=10;
const int MAX_SAMPLES=100;  
/*This is the 2d array to store sampled data*/
float data[MAX_SENSORS][MAX_SAMPLES];
/*Switch flags to indicate if a sensor is on, 1 means on, 0 means off.Initial condition can be defined by config in database.
  In setup function all flags will be at first set to 0(off) 
  Attention!!! the order of correspoding sensors should be defined and used by user exactly e.g. flag[1] represents bmp280,flag[2] represents dht11 
  This flag array is often used in many later on shown up function*/
int flag[MAX_SENSORS];
/*sensor attributes, which can be rewritten by user through nodered */
String sensor_ids[MAX_SENSORS] = {"bmp280","bmp280","dht11","st1147","button"};
String sensor_descriptions[MAX_SENSORS] = {"atmospheric pressure","altitude", "humidity","temperature","button"};
String sensor_units[MAX_SENSORS] = {"pa","m","%","°C","no unit"};
String sensor_group_ids[MAX_SENSORS] = {"","","","",""};
/*this parameter is to count the sample steps*/
int data_capture_iteration = 0;
/*timestamps container*/
String timestamps[MAX_SENSORS];
/*updata timestamp flag*/
bool update_timestamp = false;



/*bmp280 I2C and SPI class*/
Adafruit_BMP280 bmp280_i2c; 
Adafruit_BMP280 bmp280_spi(BMP280_CS, BMP280_MOSI, BMP280_MISO,  BMP280_SCK);


/*dht11 class*/
DHT dht11(SINGLE_WIRE_PIN,DHT11);


/*sampling and sending parameter of bmp280 by default
  this part is specific for bmp280 and can be neglected if using another sensor instead*/
Adafruit_BMP280::sensor_mode        working_mode=Adafruit_BMP280::MODE_NORMAL;
Adafruit_BMP280::sensor_sampling    temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X16;
Adafruit_BMP280::sensor_sampling    press_oversampling_rate=Adafruit_BMP280::SAMPLING_X16;
Adafruit_BMP280::sensor_filter      working_filter=Adafruit_BMP280::FILTER_OFF;
Adafruit_BMP280::standby_duration   working_standby_duration=Adafruit_BMP280::STANDBY_MS_250;


/*Request flags,1 means there is a setting change request, 0 means no request.
  Those flags are used at the beginning of void loop(),where the setup function like sensor.begin() are placed.
  Usually those setup function are placed at void setup(), but in order to update the parameter they have to be 
  placed in void loop() to receive possible update by user*/
int BMP280_I2C_REQUEST_FLAG=0;
int BMP280_SPI_REQUEST_FLAG=0;
int DHT11_SINGLE_WIRE_REQUEST_FLAG=0;
int SAMPLE_SWITCH_REQUEST_FLAG=0;
int FORCED_MEASUREMENT_FLAG=0;          //bmp280 special mode flag,only for bmp280


/*This flag is used to inquire config with mac address from database, will be used only one time in loop.
  If there is a corresponding config in database then the config will be put into application. If no the default 
  config in nodemcu will be used*/
int READ_CONFIG_FLAG=1;


/*array for data of forced measurement of bmp280*/
char pressure[32];
char altitude[32];


/*due to unknown reasons measurement function of dht11 library and bmp280readPressure/Altitude doesn't work well in ticker function.
 * so this global variable is created to store measured data in loop and will be accessed in 
 *"data_capture" function */
float humidity=-1;
float atmospheric_pressure=-1;
float local_altitude=-1;


/*NTP and time settings*/
unsigned long t_stamp_millis_init;
unsigned long t_stamp_millis;
unsigned long t_stamp_seconds;
char t_stamp_millis_formatted[3];
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);


/*MES connection infos*/
IPAddress remoteIp(192, 168, 43, 1);
unsigned short remotePort = 1337;
unsigned short localPort = 1337;


/*JSON Settings*/
bool send_json = false;
char json_out[2048];
char json_config[2048];


/*Ticker*/
Ticker ticker;


/*wifi parameter*/
const char* ssid = "Hotspot111";
const char* password = "sffe2541";
const IPAddress mqtt_server(192,168,43,10);


/*mqtt client parameter and setting*/
WiFiClient espClient;
PubSubClient client(espClient);


/*set up wifi*/
void setup_wifi(){
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);

  WiFi.begin(ssid,password);
  IPAddress ip(192,168,43,50);
  IPAddress gateway(192,168,43,1);
  IPAddress subnet(255,255,255,0);
  IPAddress dns(8,8,8,8);
  WiFi.config(ip,gateway,subnet,dns);
 
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address:");
  Serial.println(WiFi.localIP());
}


/* This function shows up in void callback() function.
 * It is used to compare a byte* payload and a given char* content
 * determine whether their contents are the same or not.
 * Length is given by callback function, indicating the length of payload*/
bool compare(byte* payload, char* content, unsigned int length){
  int content_length=0;
  while(content[content_length]!='\0'){
    content_length++;
  }
  if(content_length!=length){
    return false;
  }
  else{
    for(int i=0;i<length;i++){
      if((char)payload[i]!=content[i])
        return false;
    }
    return true;
  }
}



/*callback function of mqtt client, set to subscribe message*/
void callback(char* topic,byte* payload,unsigned int length){
  /*display all received topics and messages at serial port*/
  Serial.print("message arrived  ");
  Serial.print(topic);
  Serial.print(": ");
  for(int i=0;i<length;i++){
    Serial.print((char)payload[i]);
  }
  Serial.println();

  /*handle different topics, receive the payload, set request flag to 1(it means setting of sensor is to be changed )*/

  /*bmp280 sensor mode*/
  if(String(topic)==String("bmp280 sensor mode")){
      if(compare(payload,"MODE_SLEEP",length))
        {working_mode=Adafruit_BMP280::MODE_SLEEP;}
      else if(compare(payload,"MODE_NORMAL",length))
        {working_mode=Adafruit_BMP280::MODE_NORMAL;}
      else if(compare(payload,"MODE_FORCED",length))
        {working_mode=Adafruit_BMP280::MODE_FORCED;FORCED_MEASUREMENT_FLAG=1;}
      else if(compare(payload,"MODE_SOFT_RESET_CODE",length))
        {working_mode=Adafruit_BMP280::MODE_SOFT_RESET_CODE;}
      else {}
  }
  /*bmp280 temperature oversampling rate*/
  else if(String(topic)==String("bmp280 temp oversampling rate")){
      if(compare(payload,"SAMPLING_NONE",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_NONE;}
      else if(compare(payload,"SAMPLING_X1",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X1;}
      else if(compare(payload,"SAMPLING_X2",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X2;}
      else if(compare(payload,"SAMPLING_X4",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X4;}
      else if(compare(payload,"SAMPLING_X8",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X8;}
      else if(compare(payload,"SAMPLING_X16",length))
        {temp_oversampling_rate=Adafruit_BMP280::SAMPLING_X16;}
      else{}
  }
  /*bmp280 pressure oversampling rate*/
  else if(String(topic)==String("bmp280 press oversampling rate")){
      if(compare(payload,"SAMPLING_NONE",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_NONE;}
      else if(compare(payload,"SAMPLING_X1",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_X1;}
      else if(compare(payload,"SAMPLING_X2",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_X2;}
      else if(compare(payload,"SAMPLING_X4",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_X4;}
      else if(compare(payload,"SAMPLING_X8",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_X8;}
      else if(compare(payload,"SAMPLING_X16",length))
        {press_oversampling_rate=Adafruit_BMP280::SAMPLING_X16;}
      else{}
  }
  /*bmp280 sensor filter rate*/
  else if(String(topic)==String("bmp280 sensor filter")){
      if(compare(payload,"FILTER_OFF",length))
        {working_filter=Adafruit_BMP280::FILTER_OFF;}
      else if(compare(payload,"FILTER_X2",length))
        {working_filter=Adafruit_BMP280::FILTER_X2;}
      else if(compare(payload,"FILTER_X4",length))
        {working_filter=Adafruit_BMP280::FILTER_X4;}
      else if(compare(payload,"FILTER_X8",length))
        {working_filter=Adafruit_BMP280::FILTER_X8;}
      else if(compare(payload,"FILTER_X16",length))
        {working_filter=Adafruit_BMP280::FILTER_X16;}
      else{}
  }
  /*bmp280 standby duration*/
  else if(String(topic)==String("bmp280 standby duration")){
      if(compare(payload,"STANDBY_MS_1",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_1;}
      else if(compare(payload,"STANDBY_MS_63",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_63;}
      else if(compare(payload,"STANDBY_MS_125",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_125;}
      else if(compare(payload,"STANDBY_MS_250",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_250;}
      else if(compare(payload,"STANDBY_MS_500",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_500;}
      else if(compare(payload,"STANDBY_MS_1000",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_1000;}
      else if(compare(payload,"STANDBY_MS_2000",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_2000;}
      else if(compare(payload,"STANDBY_MS_4000",length))
        {working_standby_duration=Adafruit_BMP280::STANDBY_MS_4000;}
      else{}
  }
  /*bmp280 i2c pin*/
  else if(String(topic)==String("bmp280 i2c")){
      if(compare(payload,"on",length)){
        flag[0]=1;
        BMP280_I2C_REQUEST_FLAG=1;
      }
      else if(compare(payload,"off",length)){
        flag[0]=0;
        BMP280_I2C_REQUEST_FLAG=1;
      }
      else{}
      send_config("config/update");
  }
  /*bmp280 spi pin*/
  else if(String(topic)==String("bmp280 spi")){
      if(compare(payload,"on",length)){
        flag[1]=1;
        BMP280_SPI_REQUEST_FLAG=1;
      }
      else if(compare(payload,"off",length)){
        flag[1]=0;
        BMP280_SPI_REQUEST_FLAG=1;
      }
      else{}
      send_config("config/update");
  }
  /*dht11 single wire pin*/
  else if(String(topic)==String("dht11 single wire")){
      if(compare(payload,"on",length)){
        flag[2]=1;
        DHT11_SINGLE_WIRE_REQUEST_FLAG=1;
      }
      else if(compare(payload,"off",length)){
        flag[2]=0;
        DHT11_SINGLE_WIRE_REQUEST_FLAG=1;
      }
      else{}
      send_config("config/update");
  }
  /*st1147 analog measurement switch*/
  else if(String(topic)==String("st1147 measurement")){
      if(compare(payload,"on",length)){
        flag[3]=1;
      }
      else if(compare(payload,"off",length)){
        flag[3]=0;
      }
      else{}
      send_config("config/update");
  }
  /*button digital measurement switch*/
  else if(String(topic)==String("button switch module")){
      if(compare(payload,"on",length)){
        flag[4]=1;
      }
      else if(compare(payload,"off",length)){
        flag[4]=0;
      }
      else{}
      send_config("config/update");
  }
  /*sample rate setup*/
  else if(String(topic)==String("sample rate")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      SAMPLERATE=atof(new_payload);
      SAMPLE_SWITCH_REQUEST_FLAG=1;
      send_config("config/update");
  }
  /*general sample switch*/
  else if(String(topic)==String("sample switch")){
      if(compare(payload,"on",length)){
        SAMPLE_SWITCH=1;
        SAMPLE_SWITCH_REQUEST_FLAG=1;
      }
      else if(compare(payload,"off",length)){
        SAMPLE_SWITCH=0;
        SAMPLE_SWITCH_REQUEST_FLAG=1;
      }
      send_config("config/update");
  }
  /*spi cs pin setup*/
  else if(String(topic)==String("spi cs")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_CS=(int8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*spi mosi pin setup*/
  else if(String(topic)==String("spi mosi")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_MOSI=(int8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*spi miso pin setup*/
  else if(String(topic)==String("spi miso")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_MISO=(int8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*spi sck pin setup*/
  else if(String(topic)==String("spi sck")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_SCK=(int8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*i2c sda pin setup*/
  else if(String(topic)==String("i2c sda")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_SDA=atoi(new_payload);
      send_config("config/update");
  }
  /*i2c scl pin setup*/
  else if(String(topic)==String("i2c scl")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      BMP280_SCL=atoi(new_payload);
      send_config("config/update");
  }
  /*single wire pin setup*/
  else if(String(topic)==String("single wire pin")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      SINGLE_WIRE_PIN=(uint8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*digital pin setup*/
  else if(String(topic)==String("digital pin")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      DIGITAL_PIN=(uint8_t)atoi(new_payload);
      send_config("config/update");
  }
    /*analog pin setup*/
  else if(String(topic)==String("analog pin")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      ANALOG_PIN=(uint8_t)atoi(new_payload);
      send_config("config/update");
  }
  /*sample amount in each json*/
  else if(String(topic)==String("sample amount")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      int number=atoi(new_payload);
      if(number>MAX_SAMPLES)
        number=MAX_SAMPLES;
      SAMPLES=number;
      send_config("config/update");
  }
  /*reiceiving configuration after publishing inquirement*/
  else if(String(topic)==String("config/from_database")){
      char new_payload[length+1];
      new_payload[length]='\0';
      for(int i=0;i<length;i++){
         new_payload[i]=char(payload[i]);
      }
      /*if no config in database, default config will be used"*/
      if(compare(payload,"no config in database",length)){
        send_config("config/default_nodemcu");
        Serial.println("no config for this sensor platform in database, default config is used");
      }
      /*if there is config for this nodemcu, then read config*/
      else{
        DynamicJsonDocument input_doc(8192);
        deserializeJson(input_doc,new_payload);
        
        SENSORS=input_doc["sensor_amount"];

        for(int i=0;i<input_doc["sensor_amount"];i++){
        
          const char* data1=input_doc["sensor_ids"][i];
          sensor_ids[i]=String(data1);

          const char* data2=input_doc["sensor_descriptions"][i];
          sensor_descriptions[i]=String(data2);

          const char* data3=input_doc["sensor_group_ids"][i];
          sensor_group_ids[i]=String(data3);

          const char* data4=input_doc["sensor_units"][i];
          sensor_units[i]=String(data4);
        }      
        Serial.println("config from database is used");
      }
  }
  else{}
}


/*reconnection function of mqtt client, loop to reconnect to mqtt broker*/
void reconnect(){
  while(!client.connected()){
    Serial.print("Attempting MQTT connection...");
    String clientId="sensor platform";
    clientId+=WiFi.macAddress();
    if(client.connect(clientId.c_str())){
      Serial.println("connected");
      client.publish("platform_connection_status","sensor platform is now connected");
      client.subscribe("bmp280 sensor mode");
      client.subscribe("bmp280 temp oversampling rate");
      client.subscribe("bmp280 press oversampling rate");
      client.subscribe("bmp280 sensor filter");
      client.subscribe("bmp280 standby duration");
      client.subscribe("bmp280 i2c");
      client.subscribe("bmp280 spi");
      client.subscribe("dht11 single wire");
      client.subscribe("st1147 measurement");
      client.subscribe("button switch module");
      client.subscribe("sample rate");
      client.subscribe("sample switch");
      client.subscribe("spi cs");
      client.subscribe("spi mosi");
      client.subscribe("spi miso");
      client.subscribe("spi sck");
      client.subscribe("i2c sda");
      client.subscribe("i2c scl");
      client.subscribe("single wire pin");
      client.subscribe("digital pin");
      client.subscribe("analog pin");
      client.subscribe("sample amount");
      client.subscribe("config/from_database");
    }
    else{
      Serial.print("failed, state=");
      Serial.print(client.state());
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
  }
}


/*st1147 analog temperature sensor, this function is used to measure temperature*/
double st1147(int RawADC){
  if(RawADC==4095)
    RawADC--;//because denominator in later formula can't be 0
  double Temp;
  Temp=log(RawADC*10000/(4095-RawADC));
  Temp=1/(0.001129148+(0.000234125+(0.0000000876741*Temp*Temp))*Temp);
  Temp=Temp-273.15;
  return Temp;
}


String timestamp() {
  String ts;
  t_stamp_millis = ((millis() - t_stamp_millis_init) % 1000);
  sprintf(t_stamp_millis_formatted, "%03d", t_stamp_millis);
  ts = (timeClient.getFormattedTime() + ":" + t_stamp_millis_formatted);
  //Serial.println(ts);
  return ts;
}

void data_capture() {
  int sensor_connection_flag=1;                                                     //Local flag, 1 means all sensors are physically connected
  //read the sensor data
  /*BMP280 i2c reading under mode normal*/  
  if(flag[0]==1 && working_mode==Adafruit_BMP280::MODE_NORMAL){
    /* Due to unknown reasons this bmp280_i2c.readPressure()
     *  doesn't work and leads to crash of program. So the reading
     *  is in loop. Value is stored in global variable and read here.
     */
     
    /*
    if(bmp280_i2c.is_connected()){                                                  //is_connected function checks connection
      data[0][data_capture_iteration] = bmp280_i2c.readPressure();                  //by reading sensor id
    }                                                                               
    else{                                                                           
      Serial.println("Could not find a valid BMP280 sensor, check i2c wiring!");
      sensor_connection_flag=0;
    }
    */
    data[0][data_capture_iteration]=atmospheric_pressure;
  }
  /*BMP280 spi reading under mode normal*/
  if(flag[1]==1 && working_mode==Adafruit_BMP280::MODE_NORMAL){
    /* Due to unknown reasons this bmp280_i2c.readPressure()
     *  doesn't work and leads to crash of program. So the reading
     *  is in loop. Value is stored in global variable and read here.
     */
     
    /*         
    if(bmp280_spi.is_connected()){
      data[1][data_capture_iteration] = bmp280_spi.readAltitude(1013.25);
    }
    else{
      Serial.println("Could not find a valid BMP280 sensor, check spi wiring!");
      sensor_connection_flag=0;
    }
    */    
    data[1][data_capture_iteration]=local_altitude;
  }
  /*DHT11 reading*/
  if(flag[2]==1){
    if(!isnan(humidity)){                                                           //isnan function check the humidity if it is NAN
      data[2][data_capture_iteration] = humidity ;
    }
    else{
      Serial.println("Could not find a valid dht11, check single wire connection!");
      sensor_connection_flag=0;
    }
  }
  /*st1147 reading*/
  if(flag[3]==1){
    if(st1147(analogRead(ANALOG_PIN))<=40&&st1147(analogRead(ANALOG_PIN))>0)                        //Wrong value is usually not in the range and filtered out
      data[3][data_capture_iteration] = st1147(analogRead(ANALOG_PIN));
    else{
      Serial.println("Could not find a valid st1147, check analog connection!");
      sensor_connection_flag=0;
    }
  }
  /*Button*/
  /*Temporarily there is no method to check if digital connection is there.Only by long pressing the button and checking
  if there is still 1(in principle it should be all 0) user can find out if sensor is connected*/
  if(flag[4]==1){
    data[4][data_capture_iteration] = digitalRead(DIGITAL_PIN);
  }

  /* Set new timestamp if new json is to be generated*/
  if (data_capture_iteration == 0) {
    String ts = timestamp();
    for (int i = 0; i < MAX_SENSORS; i++) {
      timestamps[i] = ts;
    }
  }
  /*Increase capture iteration under the circumstance that sensors are connected and data are measured.
    Otherwise this iteration will be implemented again
    Not measured data in container data[][] is by default -1.*/
  if(sensor_connection_flag==1){
    data_capture_iteration++;
  }
  
  /*send json_send flag if data complete*/
  if (data_capture_iteration >= SAMPLES) {
    send_json = true;
  }
}


/*Function to generate a json message*/
void json_create_msg(String controller_id, float data[MAX_SENSORS][MAX_SAMPLES], String sensor_ids[MAX_SENSORS]) {
  DynamicJsonDocument doc(8192);
  //DynamicJsonDocument *doc = new DynamicJsonDocument(8192);
  JsonObject payload = doc.createNestedObject("payload");

  JsonArray payload_sensor = payload.createNestedArray("sensor");
  
  for (int i = 0; i < MAX_SENSORS; i++) {
    /* if sensor is not active or connected, skip this iteration of loop, if active, proceed*/
    if(flag[i]==1){
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
        if(data[i][j]!=-1)
          payload_sensor_0_data_values.add(data[i][j]);
      }
    }
  }


  JsonObject meta = doc.createNestedObject("meta");
  JsonObject meta_controller = meta.createNestedObject("controller");
  meta_controller["id"] = controller_id;

  //serializeJson(doc, Serial);
  //Serial.println("");
  ntpUDP.beginPacket(remoteIp, remotePort);
  serializeJson(doc, ntpUDP);
  ntpUDP.println();
  ntpUDP.endPacket();
  serializeJson(doc,json_out);
}


/*this function is used to create a config json message and update the database config information*/
void send_config(char* topic){
  DynamicJsonDocument doc(8192);
  JsonObject configuration=doc.createNestedObject();
  configuration["_id"]=WiFi.macAddress();
  configuration["sensor_amount"]=SENSORS;
  JsonArray sensorID=configuration.createNestedArray("sensor_ids");
  JsonArray sensorDescriptions=configuration.createNestedArray("sensor_descriptions");
  JsonArray sensorUnits=configuration.createNestedArray("sensor_units");
  JsonArray sensorGroupID=configuration.createNestedArray("sensor_group_ids");
  for(int i=0;i<SENSORS;i++){
    sensorID.add(sensor_ids[i]);
    sensorDescriptions.add(sensor_descriptions[i]);
    sensorUnits.add(sensor_units[i]);
    sensorGroupID.add(sensor_group_ids[i]);
  }
  configuration["sample"]=SAMPLES;
  configuration["sample_rate"]=SAMPLERATE;
  configuration["sample_switch"]=SAMPLE_SWITCH;
  JsonArray initialStatus=configuration.createNestedArray("initial_status_of_sensors");
  for(int i=0;i<SENSORS;i++)
    initialStatus.add(flag[i]);
  JsonObject spi_pins=configuration.createNestedObject("spi_pins");
  spi_pins["sck"]=BMP280_SCK;
  spi_pins["miso"]=BMP280_MISO;
  spi_pins["mosi"]=BMP280_MOSI;
  spi_pins["cs"]=BMP280_CS;
  JsonObject i2c_pins=configuration.createNestedObject("i2c_pins");
  i2c_pins["sda"]=BMP280_SDA;
  i2c_pins["scl"]=BMP280_SCL;
  configuration["single_wire_pin"]=SINGLE_WIRE_PIN;
  configuration["digital_pin"]=DIGITAL_PIN;
  configuration["analog_pin"]=ANALOG_PIN;

  serializeJson(doc,json_config);
  client.publish(topic,json_config);
}


void setup() {
  /*set every flag to 0(off) including those position without sensors,this is default condition*/
  for(int i=0;i<MAX_SENSORS;i++){
    flag[i]=0;
  }
  /*set every data to -1(default value)*/
  for (int i = 0; i < MAX_SENSORS; i++) {
    for (int j = 0; j < MAX_SAMPLES; j++) {
      data[i][j] = -1;
    }
  }
  
  Serial.begin(115200);
  pinMode(DIGITAL_PIN,INPUT);

  /*setup wifi*/
  setup_wifi();
  
  /*set mqtt*/
  client.setServer(mqtt_server,1883);
  client.setCallback(callback);
  
  /*get inital ntp packet from server*/
  Serial.print("Update time by NTP");
  timeClient.begin();
  while (timeClient.getEpochTime() < 10000) {
    timeClient.update();
    Serial.print(".");
  }
  Serial.println();
  t_stamp_millis_init = millis();
}

void loop() {
/*try to connect to begin with,or try to reconnect when in disconnected condition*/
  if (!client.connected()) {
    reconnect();
  }


/*try to handle the callback function,i.e. handle the coming mqtt message*/
  client.loop();


/*ntp update, acquire current time*/
  timeClient.update();                                      

/*this flag is used to read config from database, it is used only one time in loop during initiation to inquire with mac address
if there is a corresponding config. if no use the default config, if yes use config in database */
  if(READ_CONFIG_FLAG==1){
    client.publish("config/initiate",WiFi.macAddress().c_str());
    READ_CONFIG_FLAG=0;
  }


  /*in following sections the new setting of sensors will be made when requests of reset arrive */                                  

  /*i2c setup*/
  if(BMP280_I2C_REQUEST_FLAG==1){
    /*begin the bmp280 i2c*/
    if(flag[0]==1){                                 
      bmp280_i2c.i2c_pins(BMP280_SDA,BMP280_SCL);
      while(!bmp280_i2c.begin()){
        Serial.println("Could not find a valid BMP280 sensor, check i2c wiring!");
        client.publish("bmp280 i2c wiring","Could not find a valid BMP280 sensor, check i2c wiring!");
        delay(2000);
      }
      bmp280_i2c.setSampling(working_mode,            /* Operating Mode. */
                          temp_oversampling_rate,     /* Temp. oversampling */
                          press_oversampling_rate,    /* Pressure oversampling */
                          working_filter,             /* Filtering. */
                          working_standby_duration);  /* Standby time. */
      BMP280_I2C_REQUEST_FLAG=0;
      client.publish("bmp280 i2c setup","bmp280 i2c is now on and mode is set up");
    }
    /*shut down the bmp280 i2c*/
    else{
      digitalWrite(BMP280_SDA, 0);                       
      digitalWrite(BMP280_SCL, 0);
      BMP280_I2C_REQUEST_FLAG=0;
      client.publish("bmp280 i2c setup","bmp280 i2c is now off");
    }
  }
  if(flag[0]==1)
    atmospheric_pressure=bmp280_i2c.readPressure();
  else
    atmospheric_pressure=-1;


  /*spi setup*/
  if(BMP280_SPI_REQUEST_FLAG==1){
    /*begin the bmp280 spi*/
    if(flag[1]==1){ 
      bmp280_spi.spi_pins(BMP280_CS,BMP280_MOSI,BMP280_MISO,BMP280_SCK);                            
      while(!bmp280_spi.begin()){
        Serial.println("Could not find a valid BMP280 sensor, check spi wiring!");
        client.publish("bmp280 spi wiring","Could not find a valid BMP280 sensor, check spi wiring and pin setting!");
        delay(2000);
      }
      bmp280_spi.setSampling(working_mode,            /* Operating Mode. */
                          temp_oversampling_rate,     /* Temp. oversampling */
                          press_oversampling_rate,    /* Pressure oversampling */
                          working_filter,             /* Filtering. */
                          working_standby_duration);  /* Standby time. */
      BMP280_SPI_REQUEST_FLAG=0;
      client.publish("bmp280 spi setup","bmp280 i2c is now switched on and finished setup");
    }
    /*shut down the bmp280 spi*/
    else{                                             
      pinMode(BMP280_SCK,INPUT); 
      pinMode(BMP280_MISO,INPUT);
      pinMode(BMP280_MOSI,INPUT);
      pinMode(BMP280_CS,INPUT);   
      BMP280_SPI_REQUEST_FLAG=0;
      client.publish("bmp280 spi setup","bmp280 spi is now off");  
    }
  }
  if(flag[1]==1)
    local_altitude=bmp280_spi.readAltitude(1013.25);
  else
    local_altitude=-1;

  /*single wire setup*/
  if(DHT11_SINGLE_WIRE_REQUEST_FLAG==1){ 
    /*begin the DHT11 single wire transmission*/
    if(flag[2]==1){
      dht11.single_wire_pin(SINGLE_WIRE_PIN);
      dht11.begin();
      client.publish("dht11 setup","dht11 single wire is now switched on");
    }
    /*shut down the DHT11 single wire transmission*/
    else{
      pinMode(SINGLE_WIRE_PIN,INPUT);
      client.publish("dht11 setup","dht11 single wire is now switched off");
    }
    DHT11_SINGLE_WIRE_REQUEST_FLAG=0;
  }


  /*humidity reading*/
  if(flag[2]==1)
    humidity=dht11.readHumidity();
  else
    humidity=-1;

  /*sample switch setup*/
  if(SAMPLE_SWITCH_REQUEST_FLAG==1){
    /*start the ticker function, set the ticker function once samplerate is changed*/
    if(SAMPLE_SWITCH==1){
      ticker.attach(1/SAMPLERATE, data_capture);
      client.publish("sample switch status","sample is now on, sample rate is setup");
    }
    /*shut down the ticker function*/
    else{
      ticker.detach();
      client.publish("sample switch status","sample is now off");
    }
    SAMPLE_SWITCH_REQUEST_FLAG=0;
  }
    

/*extra sending in loop function i.e. forced measurement of bmp280*/
/*BMP280 forced measurement*/
  if(flag[1]==1 && bmp280_spi.is_connected() && working_mode==Adafruit_BMP280::MODE_FORCED&&FORCED_MEASUREMENT_FLAG==1){  /*forced measurement of spi */
    if(bmp280_spi.is_connected()){
      bmp280_spi.takeForcedMeasurement();                                                         /*only take one measurement */                                     
                                                                                                  /*then go to sleep mode     */
      snprintf(altitude,sizeof altitude,"%f",bmp280_spi.readAltitude(1013.25));
      client.publish("spi altitude",altitude);
    }
    else{
      client.publish("spi altitude","please check spi wiring");
    }

    FORCED_MEASUREMENT_FLAG=0;
  }
  if(flag[0]==1 && bmp280_i2c.is_connected() && working_mode==Adafruit_BMP280::MODE_FORCED&&FORCED_MEASUREMENT_FLAG==1){  /*forced measurement of i2c */
    if(bmp280_i2c.is_connected()){
      bmp280_i2c.takeForcedMeasurement();                                                         /*only take one measurement */
                                                                                                  /*then go to sleep mode     */
      snprintf(pressure,sizeof pressure,"%f",bmp280_i2c.readPressure());
      client.publish("i2c pressure",pressure);
    }
    else{
      client.publish("i2c pressure","please check i2c wiring");
    }
    FORCED_MEASUREMENT_FLAG=0;
  }
  

/*json message sending*/
  if (send_json == true) {    
    json_create_msg(WiFi.macAddress(), data, sensor_ids);
    Serial.println(json_out);
    client.publish("data",json_out);
    client.publish("mes",json_out);
    send_json = false;
    data_capture_iteration = 0;
    
    for (int i = 0; i < MAX_SENSORS; i++) {
      for (int j = 0; j < MAX_SAMPLES; j++) {
        data[i][j] = -1;
      }
    }
  }
}
