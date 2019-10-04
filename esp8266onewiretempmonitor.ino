/*

Chambeers temperature monitor

*/
#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <InfluxDb.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <DNSServer.h>
#include <WiFiManager.h> 
#include <ArduinoJson.h> 

//
MDNSResponder mdns;
ESP8266WebServer server(80);

// Web Hook handlers
void NotFound_page();
// CONNECTED:
void connected_status_page();
void clear_eeprom_page();
void pause_measurements_page();
// AP:
void configure_system_page();
void handle_configure();

String st;
String wifi_choice="<select name=\"ssid\">";

// OneWire sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 13
#define TEMPERATURE_PRECISION 12
#define MAX_DEVICES 10
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Adafruit BME280 
#include <Adafruit_BME280.h>
#define BME280_I2C_ADDRESS  0x76
#define SEALEVELPRESSURE_HPA (1011.52)
Adafruit_BME280  bme;
bool havebme280 = false;

// EEPROM Layout end of possible configuration data
// 0 SSID 32 PASS 96 DB Host 160 DB Name 224 Set 240
#define EEPROM_SSID_O 0
#define EEPROM_SSID_S 32
#define EEPROM_PASS_O 32
#define EEPROM_PASS_S 64
#define EEPROM_DBH_O 96
#define EEPROM_DBH_S 64
#define EEPROM_DBN_O 160
#define EEPROM_DBN_S 64
#define EEPROM_SET_O 224
#define EEPROM_SET_S 16
#define EEPROM_SIZE 240

// Quick way to tell if DB parameters have been set
String isset;
// InfluxDB parameters
String influxdbhost;
String influxdbname;

//flag for saving data
bool shouldSaveConfig = false;

// How often to post to influx
const unsigned long postRate = 60000;
unsigned long lastPost = 0;

// Identifier for configuration SSID and device in influx
String postedID = "ccESP8266-";

// Storage for OneWire device names and measurements
DeviceAddress devices[MAX_DEVICES];
double measurements[MAX_DEVICES];
String names[MAX_DEVICES];

// Detected number of devices - occurs at Power On/Reset only
// TODO: endpoint to rescan the OneWire network
int num_devices = 0;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// Setup some base values
void init_values()
{
  for (int x=0; x<MAX_DEVICES; x++)
  {
    names[x] = "NC";
    measurements[x] = 0.0;
  }
}

// Takes a OneWire device address and converts it to a printable HEX String
String convertDeviceAddress(DeviceAddress da)
{   
    String tempString = "";
     for (uint8_t i = 0; i < 8; i++)
     {
         // zero pad the address if necessary
         if (da[i] < 16) tempString += "0";
         tempString += String(da[i], HEX);
      }
  return (tempString);
}

// Tell all OneWire devices to retrieve temperatures
void GetTemps()
{
  sensors.requestTemperatures(); // Send the command to get temperatures
  for (int i=0; i<num_devices; i++)
  {
    measurements[i] = sensors.getTempFByIndex(i);
  }
}

// Post measurements to influx
int postToinfluxDB()
{ 
  Influxdb influx(influxdbhost.c_str());

  influx.setDb(influxdbname.c_str());

  bool post = false;
  bool success = true;
  
  // Get current temps
  GetTemps();
 
  // Batch up measurements 
  for (int i=0; i < num_devices; i++)
  {    
      // Only send valid sensor data to influx
      if ( names[i] != "NC" ) 
      {
         post = true;
         InfluxData row("temperatures");
         String id = "id" + (String)i;
         String temps = "temp" + (String)i;
      
         row.addTag("device", postedID);    
         row.addTag("sensor", names[i]);
         row.addValue("temp", measurements[i]);
         
         // Print our stuff out to serial
         Serial.print(id);
         Serial.print(": ");
         Serial.print(names[i]);
         Serial.print(" :");
         Serial.print(" Temp: ");
         Serial.println(measurements[i]);
         influx.prepare(row);
      }
  }
  if ( havebme280 )
  {
    // We haev a BME280 connected as well
    InfluxData row("temperatures");
    String sensor = postedID + "-BME280";
 
    row.addTag("device", postedID);    
    row.addTag("sensor", sensor);
    row.addValue("temp", ((bme.readTemperature() * 1.8)+32) );
    row.addValue("pressure", (bme.readPressure() / 100.0F));
    row.addValue("humidity", (bme.readHumidity()));
    influx.prepare(row);
    post=true;
  }
  
  if ( post )
    success = influx.write();

  return (success); // Return success
}

bool check_for_bme280( void ) {
    Wire.begin();
    if (bme.begin(BME280_I2C_ADDRESS) == 0 )
    {  // connection error or device address wrong!
        Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
        Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
        Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
        Serial.print("        ID of 0x60 represents a BME 280.\n");
        Serial.print("        ID of 0x61 represents a BME 680.\n");
        return(false);
    }
    return(true);
}

// Start here
void loop ( void ) {
  server.handleClient();
  if ( isset == "SET") {
    if ((lastPost + postRate <= millis()) || lastPost == 0)
    {
      if (postToinfluxDB())
      {
        lastPost = millis();
      }
      else // If the post failed
      {
        delay(500); // Short delay, then try again
        Serial.println(F("influxDB Post failed, will try again."));
      }
    }
  }
  delay(50);
}

// Generic EEPROM String reader
String read_eeprom(int offset, int stringlength) {
  String tempstring;
  for (int i=offset; i < (offset+stringlength); ++i)
  {
    tempstring += char(EEPROM.read(i));
  }
  return (tempstring);
}

void write_eeprom(int offset, String savestring) {
  // TODO: Convert this to use put
  for (int i = 0; i < savestring.length(); ++i)
  {
     EEPROM.write(offset+i, savestring[i]);
     // TODO: Add debug statement
     //Serial.print("Wrote: ");
     //Serial.print(savestring[i]); 
     //Serial.println();
  }
}

int testWifi(void) {
  int c = 0;
  Serial.println("Waiting for Wifi to connect");  
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) { return(WL_CONNECTED); } 
    delay(500);
    Serial.print(".");    
    c++;
  }
  Serial.println("Connect timed out, opening AP");
  return(WiFi.status());
} 

void setupAP(void) {
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
     {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      delay(10);
     }
  }
  Serial.println(""); 
  st = "<ul>";
  for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      st +=i + 1;
      st += ": ";
      st += WiFi.SSID(i);
      // Add the found network to a input option selection 
      wifi_choice +="<option value=\"";
      wifi_choice += WiFi.SSID(i);
      wifi_choice += "\">";
      wifi_choice += WiFi.SSID(i);
      wifi_choice += "</option>";
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      st += "</li>";
    }
  wifi_choice += "<option value=\"Unlisted\">Unlisted</option>";
  wifi_choice += "</select>";
  st += "</ul>";
  delay(100);
  
  WiFi.softAP(postedID);
  Serial.println("softap");
  Serial.println("");
 
}

/* =======================
 *  Start Connected handlers
 */
void NotFound_page() {
    // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
    server.send(404, "text/plain", "404: Not found"); 
}

void connected_status_page() {
    String s;
    GetTemps();
   
    s += "<head><style>\r\ntable, th, td {\r\n border: 1px solid black;\r\n}</style></head>";
    s += "<body>\r\n";
    s += "<p>";
    s += "<table>";
    s += "<tr><th colspan=\"2\">";
    s += postedID;
    s += "</th></tr>";
    for (int i=0; i<MAX_DEVICES; i++)
    {
        s += "<tr><td>";
        s += names[i];
        s += "</td><td>";
        s += measurements[i];
        s += "</td></tr>";
    }
    s += "</table></body>";
    if ( isset == "PAUSED" ) {
        s += "<H2>Measurements are PAUSED</H2>";
    }
    server.send(200, "text/html", s); 
}

void clear_eeprom_page() {
    String s;
    s += "<h1>";
    s += postedID;
    s += "</h1>";
    s += "<p>Clearing the EEPROM<p>";
    server.send(200, "text/html", s);
    
    Serial.println("clearing eeprom");
    for (int i = 0; i < EEPROM_SIZE; ++i) { EEPROM.write(i, 0); }
    EEPROM.commit();
    Serial.println("RESETTING");
    delay(3000);
    ESP.reset();
}

void pause_measurements_page() {
      String s;

      /* TODO: Add the following:
       *     <head>
                 <meta http-equiv="refresh" content="3;url=http://IP_ADDR/" />
              </head>
      */
      isset = "PAUSED";
      s = "Hello from ";
      s += postedID;
      s += "<p>Pausing Temp Measurements<p>";
      server.send(200, "text/html", s);
}

void start_measurements_page() {
    String s;   
    // TODO: See pause - add redirect 
    isset = "SET";
    s = "Hello from ";
    s += postedID;
    s += "<p>Starting Temp Measurements<p>";
    server.send(200, "text/html", s);
        
}

/* =======================
 *  Start AP handlers
 */

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
void configure_system_page() {
    String s;
    // TODO: Add a configure password
    IPAddress ip = WiFi.softAPIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    s = "Hello from ESP8266 at ";
    s += ipStr;
    s += "<p>";
    s += st;
    s += "<form method='POST' action='/configure'>";
    s += wifi_choice; 
    s += "<p><label>Pass:</label><input name='pass' length=64><p>";
    s += "<label>DB Host</label><input name='dbhost' length=64><p><label>DB Name</label><input name='dbname' length=64><input type='submit'></form>";
    // TODO: Add new fields:
    // Checkbox - Start measurements on boot - enabled by default
    // text - postrate - Allow changes to frequency of measurements
    server.send(200, "text/html", s); 
}

void handle_configure() {
    String s;
    Serial.println("handle_configure() here");
    // Validate parameters are filled out
    if( ! server.hasArg("ssid") || ! server.hasArg("pass") 
        || ! server.hasArg("dbhost") || ! server.hasArg("dbname")
        || server.arg("ssid") == NULL || server.arg("pass") == NULL 
        || server.arg("dbhost") == NULL || server.arg("dbname") == NULL ) {
          server.send(400, "text/plain", "400: Invalid Request");
          return;
        }
    Serial.println("clearing eeprom");
   
    s += "<h1>Configuration Saved:</h1>";
    s += "<table>";
    s += "<tr><td>SSID:</td><td>";
    s += server.arg("ssid");
    s += "</tr></td>";
    s += "<tr><td>Password:</td><td>";
    s += server.arg("pass");
    s += "</tr></td>";
    s += "<tr><td>DB Host:</td><td>";
    s += server.arg("dbhost");
    s += "</tr></td>";
    s += "<tr><td>DB Name:</td><td>";
    s += server.arg("dbname");
    s += "</tr></td>";
    s += "</table>";
    s += "<h1>reset will occur in 5 seconds to boot into new wifi</h1>";
    server.send(200, "text/html", s); 
    delay(5000);
    ESP.reset();
}



void init_onewire ( void ) {
  
  // Start up the OneWire library
  sensors.begin(); 
 
  Serial.println(F("\n\nLocating devices..."));
  Serial.print(F("Found "));
  
  // locate devices on the bus
  num_devices = sensors.getDeviceCount();
  Serial.print(num_devices, DEC);
  Serial.println(F(" devices."));

  // report parasite power requirements
  Serial.print(F("Parasite power is: "));
  if (sensors.isParasitePowerMode()) 
  {
     Serial.println(F("ON"));
  }
  else 
  {
     Serial.println(F("OFF"));
  };
   
  Serial.print(F("Setting precision to: "));
  Serial.println(TEMPERATURE_PRECISION, DEC);
  
  sensors.setResolution(TEMPERATURE_PRECISION);

  // Too many devices are connected
  if (num_devices > MAX_DEVICES)
  {
    Serial.println(F("WARNING: Too many devices connected"));
    Serial.print(F("WARNING: device count set to "));
    Serial.println(MAX_DEVICES);
    num_devices = MAX_DEVICES;
  }
  
  for (int i=0; i < num_devices; i++)
  {
      Serial.print(F("Getting device "));
      Serial.print(i, DEC);
      Serial.println(F(" information"));
      if (!sensors.getAddress(devices[i], i)) 
      {  
         Serial.print(F("ERROR: Unable to find address for Device "));
         Serial.println(i,DEC);
      }
      names[i] = convertDeviceAddress(devices[i]);
      
      Serial.print(F("Device "));
      Serial.print(i,DEC);
      Serial.print(F(": "));
      Serial.println(names[i]);      
  }
}


void init_connected_handlers( void ) {
    Serial.println("Setting up Status handlers");
    // This is where the connected webserver pages go
    server.on("/", HTTP_GET, connected_status_page);
    server.on("/cleareeprom", HTTP_GET, clear_eeprom_page);
    server.on("/pause", HTTP_GET, pause_measurements_page);
    server.onNotFound(NotFound_page);
  
}

void init_ap_handlers( void ) {
    Serial.println("Setting up AP handlers");
    // This is where the AP setup webserver pages go
    server.on("/", HTTP_GET, configure_system_page);
    server.on("/configure", HTTP_POST, handle_configure);
    server.onNotFound(NotFound_page);
}

void setup_mdns() {
    if (!mdns.begin("esp8266", WiFi.localIP())) {
      Serial.println("Error setting up MDNS responder!");
      while(1) { 
              delay(1000);
      }
    }
}

void setup ( void ) {
    Serial.begin ( 115200 );
    //read configuration from FS json
    Serial.println("\n\nmounting FS...");
    if (SPIFFS.begin()) {
      Serial.println("mounted file system");
      if (SPIFFS.exists("/config.json")) {
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
          Serial.println("opened config file");
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);
          configFile.readBytes(buf.get(), size);
          
          DynamicJsonDocument json(size);
          DeserializationError error = deserializeJson(json, buf.get());
          serializeJsonPretty(json, Serial);
          if (! error) {
            Serial.println("\nparsed json");
            const char* buffer = json["database_host"];
            influxdbhost = String(buffer);
            buffer = json["database_name"];
            influxdbname = String(buffer);
          } else {
            Serial.println("failed to load json config");
          }
          configFile.close();
        }
      }
    } else {
      Serial.println("failed to mount FS");
    }
    Serial.println(F("\n\nStarting Network..."));
    // Set WiFi ID
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.macAddress(mac);
    String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
    macID.toUpperCase();
    postedID = "ccESP8266-" + macID;
    
    WiFiManagerParameter custom_influxdb_host("db_host", "InfluxDB Host", "" , 64);
    WiFiManagerParameter custom_influxdb_name("db_name", "InfluxDB Name", "", 64);
    WiFiManager wifiManager;
    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.addParameter(&custom_influxdb_host);
    wifiManager.addParameter(&custom_influxdb_name);

    if (!wifiManager.autoConnect(postedID.c_str(), "password")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
     } 

    influxdbhost = String( custom_influxdb_host.getValue());
    influxdbname = String( custom_influxdb_name.getValue());

    if(shouldSaveConfig) {
       Serial.println("saving config");

       DynamicJsonDocument json(4096);
       json["database_host"] = influxdbhost;
       json["database_name"] = influxdbname;
       
       File configFile = SPIFFS.open("/config.json", "w");
       if (!configFile) {
         Serial.println("failed to open config file for writing");
       }
       serializeJsonPretty(json, Serial);
       serializeJson(json, configFile);
       configFile.close();
       //end save   
    }

    init_values();
    init_onewire();
    havebme280 = check_for_bme280();
  

    Serial.print("Setting PostedID to: ");
    Serial.println(postedID);
    
    setup_mdns();
    init_ap_handlers();
    server.begin();
}
