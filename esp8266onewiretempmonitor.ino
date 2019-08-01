/*

Chambeers temperature monitor

*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <InfluxDb.h>
#include <WiFiUdp.h>
#include <Wire.h>

//
MDNSResponder mdns;
WiFiServer server(80);
String st;
String wifi_choice="<select name=\"ssid\">";

// OneWire sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 13
#define TEMPERATURE_PRECISION 12
#define MAX_DEVICES 10

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

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// InfluxDB parameters
// TODO: Auth?
String influxdbhost;
String influxdbname;

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

// Setup some base values
void init_values()
{
  for (int x=0; x<MAX_DEVICES; x++)
  {
    names[x] = "NC";
    measurements[x] = 0.0;
  }
}

// Convert double to string
char *dtostrf(double val, signed char width, unsigned char prec, char *s);

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
  
  // Get our current temps
  GetTemps();
 
  // Batch up measurements 
  for (int i=0; i < num_devices; i++)
  {    
      // Only send to influx valid sensors
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
  if ( post )
    success = influx.write();

  return (success); // Return success
}

// Start here
void loop ( void ) {
  if ( isset == "SET") {
    if ((lastPost + postRate <= millis()) || lastPost == 0)
    {
      if (postToinfluxDB())
      {
        lastPost = millis();
      }
      else // If the post failed
      {
        delay(5000); // Short delay, then try again
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
     Serial.print("Wrote: ");
     Serial.print(savestring[i]); 
     Serial.println();
  }
}

// URL request parser
// TODO: handle not found parameters
String get_parameter( String param, String request )
{
  param.concat('=');
  int paramLength = param.length();
  int paramStart = request.indexOf(param);
  int paramStop = request.indexOf('&', paramStart);

  String myValue = request.substring(paramStart+paramLength, paramStop);

  return(myValue);
}


int testWifi(void) {
  int c = 0;
  Serial.println("Waiting for Wifi to connect");  
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) { return(20); } 
    delay(500);
    Serial.print(WiFi.status());    
    c++;
  }
  Serial.println("Connect timed out, opening AP");
  return(10);
} 

void launchWeb(int webtype) {
          Serial.println("");
          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println(WiFi.softAPIP());
          if (!mdns.begin("esp8266", WiFi.localIP())) {
            Serial.println("Error setting up MDNS responder!");
            while(1) { 
              delay(1000);
            }
          }
          Serial.println("mDNS responder started");
          // Start the server
          server.begin();
          Serial.println("Server started");   
          int b = 20;
          int c = 0;
          while(b == 20) { 
               loop();   
             b = mdns1(webtype);
           }
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
  launchWeb(1);
  Serial.println("over");
}

int mdns1(int webtype)
{
  // Check for any mDNS queries and send responses
  mdns.update();
  
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return(20);
  }
  Serial.println("");
  Serial.println("New client");

  // Wait for data from client to become available
  while(client.connected() && !client.available()){
    delay(1);
   }
  
  // Read the first line of HTTP request
  String req = client.readStringUntil('\r');
  
  // First line of HTTP request looks like "GET /path HTTP/1.1"
  // Retrieve the "/path" part by finding the spaces
  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
    Serial.print("Invalid request: ");
    Serial.println(req);
    return(20);
   }
  req = req.substring(addr_start + 1, addr_end);
  Serial.print("Request: ");
  Serial.println(req);
  client.flush(); 
  String s;
  if ( webtype == 1 ) {
      if (req == "/")
      {
        IPAddress ip = WiFi.softAPIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
        s += ipStr;
        s += "<p>";
        s += st;
        s += "<form method='get' action='a'>";
        s += wifi_choice; 
        s += "<p><label>Pass:</label><input name='pass' length=64><p>";
        s += "<label>DB Host</label><input name='dbhost' length=64><p><label>DB Name</label><input name='dbname' length=64><input type='submit'></form>";
        s += "</html>\r\n\r\n";
        // TODO: Add new fields:
        // Checkbox - Start measurements on boot - enabled by default
        // text - postrate - Allow changes to frequency of measurements
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/a?ssid=") ) {
        // Set parameters in EEPROM
        // /a?ssid=blahhhh&pass=poooo&dbhost=blah.com&dbname=blah
        Serial.println("clearing eeprom");
        for (int i = 0; i < EEPROM_SIZE; ++i) { EEPROM.write(i, 0); }
        String qsid; 
        qsid = get_parameter("ssid",req);
        Serial.println(qsid);
        Serial.println("");
        String qpass = get_parameter("pass",req);
        Serial.println(qpass);
        Serial.println("");
        String qdbhost = get_parameter("dbhost",req);
        Serial.println(qdbhost);
        Serial.println("");
        String qdbname = get_parameter("dbname",req);
        Serial.println(qdbname);
        Serial.println("");
        
        Serial.println("writing eeprom ssid:");
        write_eeprom(EEPROM_SSID_O, qsid);
        Serial.println("writing eeprom pass:");
        write_eeprom(EEPROM_PASS_O, qpass);
        Serial.println("writing eeprom dbhost:");
        write_eeprom(EEPROM_DBH_O, qdbhost);
        Serial.println("writing eeprom dbname:");
        write_eeprom(EEPROM_DBN_O, qdbname);
        Serial.println("writing eeprom isset:");
        isset="SET";
        write_eeprom(EEPROM_SET_O, isset);
        isset="WAIT";
          
        EEPROM.commit();
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 ";
        s += "Found ";
        s += req;
        s += "<p> saved to eeprom... reset will occur in 5 seconds to boot into new wifi</html>\r\n\r\n";
        delay(5000);
        ESP.reset();
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }
  } 
  // webtype != 1
  else
  {
      // Status request - base url
      if (req == "/")
      {
        GetTemps();
        // TODO: Add a button to pause/start measurements
        //       Add links to clearconfig/configuredb/pause/start etc
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>";
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
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/cleareeprom") ) {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ";
        s += postedID;
        s += "<p>Clearing the EEPROM<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
        Serial.println("clearing eeprom");
        for (int i = 0; i < EEPROM_SIZE; ++i) { EEPROM.write(i, 0); }
        EEPROM.commit();
        delay(3000);
        ESP.reset();
      }
      else if ( req.startsWith("/changedb") ) {
        // TODO: change DB code here
      }
      else if ( req.startsWith("/changeinterval") ) {
        // TODO: change measurement interval - configure interval on startup
      }
      else if ( req.startsWith("/pause") ) {
        isset = "PAUSED";
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ";
        s += postedID;
        s += "<p>Pausing Temp Measurements<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
      }
      else if ( req.startsWith("/start") ) {
        isset = "SET";
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ";
        s += postedID;
        s += "<p>Starting Temp Measurements<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }       
  }
  client.print(s);
  Serial.println("Done with client");
  return(20);
}


void setup ( void ) {
  Serial.begin ( 115200 );
  
  // Start EEPROM
  EEPROM.begin(512);
  delay(10);
  
  init_values();
  
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
  
  Serial.println(F("\n\nStarting Network..."));
  // Set WiFi ID
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  postedID = "ccESP8266-" + macID;
  Serial.print("Setting PostedID to: ");
  Serial.println(postedID);
  // Read SSID/Password from EEPROM
  String esid=read_eeprom(EEPROM_SSID_O,EEPROM_SSID_S);
  Serial.print("SSID: ");
  Serial.println(esid); 

  String epass=read_eeprom(EEPROM_PASS_O,EEPROM_PASS_S);
  Serial.print("PASS: ");
  Serial.println(epass);  

  influxdbhost = read_eeprom(EEPROM_DBH_O,EEPROM_DBH_S);
  Serial.print("DB Host: ");
  Serial.println(influxdbhost);

  influxdbname = read_eeprom(EEPROM_DBN_O,EEPROM_DBN_S);
  Serial.print("DB Name: ");
  Serial.println(influxdbname);

  isset = read_eeprom(EEPROM_SET_O,EEPROM_SET_S);
  Serial.print("Set: ");
  Serial.println(isset);

  
  if ( esid.length() > 1 ) 
  {
    // test esid 
      WiFi.begin(esid.c_str(), epass.c_str());
      if ( testWifi() == 20 ) { 
          launchWeb(0);
          return;
      }
  }
  setupAP();
}
