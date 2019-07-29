/*
Chambeers temperature monitor

TODO Enhance the documentation here

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

// InfluxDB setup
// TODO update influxDBHost connection based on EEPROM setting
const char influxDBHost[] = "1.2.3.4";
Influxdb influx(influxDBHost);

const unsigned long postRate = 60000;
unsigned long lastPost = 0;

// TODO use MAC to make a unique SSID
const char* ssid = "TempLogger";

// Database
// TODO change DB name to EEPROM setting
const char *influxDBname = "testing";

String postedID = "ccESP8266-";

DeviceAddress devices[MAX_DEVICES];
double measurements[MAX_DEVICES];
String names[MAX_DEVICES];

// Number of devices
int num_devices = 0;

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

void GetTemps()
{

  sensors.requestTemperatures(); // Send the command to get temperatures
  for (int i=0; i<num_devices; i++)
  {
    measurements[i] = sensors.getTempFByIndex(i);
  }
 
}

int postToinfluxDB()
{ 
  bool post = false;
  
  // Get our current temps
  GetTemps();
  
  // Add the four field/value pairs defined by our stream:

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
    influx.write();
  // TODO - Really?  Just return success - get the status of the influx.write and return it...
  return 1; // Return success
}

// Start here
void loop ( void ) {
  if ((lastPost + postRate <= millis()) || lastPost == 0)
  {
    Serial.println(F("Posting to influx!"));
    if (postToinfluxDB())
    {
      lastPost = millis();
      Serial.println(F("Post Suceeded!"));
    }
    else // If the post failed
    {
      delay(500); // Short delay, then try again
      Serial.println(F("Post failed, will try again."));
    }
  }
  delay(50);
}

// TODO - Make these a single generic function.  pass in an offset and size, return String
String read_esid( void ) {
  String esid;
  for (int i=0; i<32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  return (esid);
}

String read_pass( void ) {
  String pass;
  for (int i=32; i<96; ++i)
  {
    pass += char(EEPROM.read(i));
  }
  return (pass);
}

String read_dbhost( void ) {
  String st;
  for (int i=96; i<160; ++i)
  {
    st += char(EEPROM.read(i));
  }
  return (st);
}

String read_dbname( void ) {
  String st;
  for (int i=160; i < 224; ++i)
  {
    st += char(EEPROM.read(i));
  }
  return (st);
}

int testWifi(void) {
  int c = 0;
  Serial.println("Waiting for Wifi to connect");  
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) { return(20); } 
    delay(500);
    // TODO - Not a fan of returning WiFi.status() prefer the ...
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
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      st += "</li>";
    }
  st += "</ul>";
  delay(100);
  WiFi.softAP(ssid);
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
        // TODO - Might be cool to be able to select an SSID from a list found via serial?
        //      - Save the list then present it as a choice
        IPAddress ip = WiFi.softAPIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
        s += ipStr;
        s += "<p>";
        s += st;
        s += "<form method='get' action='a'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64>";
        s += "<input name='dbhost' length=64><input name='dbname' length=64><input type='submit'></form>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/a?ssid=") ) {
        // /a?ssid=blahhhh&pass=poooo&dbhost=blah.com&dbname=blah
        Serial.println("clearing eeprom");
        for (int i = 0; i < 224; ++i) { EEPROM.write(i, 0); }
        String qsid; 
        qsid = req.substring(8,req.indexOf('&'));
        Serial.println(qsid);
        Serial.println("");
        String qpass;
        qpass = req.substring(req.lastIndexOf('=')+1);
        Serial.println(qpass);
        Serial.println("");
       
        
        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
          {
            EEPROM.write(i, qsid[i]);
            Serial.print("Wrote: ");
            Serial.println(qsid[i]); 
          }
        Serial.println("writing eeprom pass:"); 
        for (int i = 0; i < qpass.length(); ++i)
          {
            EEPROM.write(32+i, qpass[i]);
            Serial.print("Wrote: ");
            Serial.println(qpass[i]); 
          }    
        EEPROM.commit();
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 ";
        s += "Found ";
        s += req;
        s += "<p> saved to eeprom... reset to boot into new wifi</html>\r\n\r\n";
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }
  } 
  else
  {
      if (req == "/")
      {
        // Status page goes here
        GetTemps();
        
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
          // TODO - Update to filter out NC connections
          s += "<tr><td>";
          s += names[i];
          s += "</td><td>";
          s += measurements[i];
          s += "</td></tr>";
        }
        s += "</table></body>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/cleareeprom") ) {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266";
        s += "<p>Clearing the EEPROM<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }
        EEPROM.commit();
      }
      else if ( req.startsWith("/changedb") ) {

      }
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

  // Read SSID/Password from EEPROM
  String esid=read_esid();
  Serial.print("SSID: ");
  Serial.println(esid); 

  String epass=read_pass();
  Serial.print("PASS: ");
  Serial.println(epass);  

  if ( esid.length() > 1 ) 
  {
    // test esid 
      WiFi.begin(esid.c_str(), epass.c_str());
      if ( testWifi() == 20 ) { 
           // Setup connection to influxDB
          influx.setDb(influxDBname);
          uint8_t mac[WL_MAC_ADDR_LENGTH];
          WiFi.macAddress(mac);
          String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                         String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
          macID.toUpperCase();
          postedID = "ccESP8266-" + macID;
          launchWeb(0);
          return;
      }
  }
  setupAP();
}
