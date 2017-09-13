// Voltaic Water Quality Monitor
// Author: Dylan Sri-Jayantha
// Date: 13 September, 2017
//
// The following code controls an Adafruit Feather FONA 32u4 integrated with Atlas Scientific water sensors.
// Data is transmitted via MQTT packets over cellular network to the designated AdafruitIO server.
// Power-saving sleep cycles and network signal verification have been implemented.

#include <Adafruit_SleepyDog.h>
#include <SoftwareSerial.h>
#include "Adafruit_FONA.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_FONA.h"

/*************************** FONA Pins ***********************************/
// Default pins for Feather 32u4 FONA
#define FONA_RX  9
#define FONA_TX  8
#define FONA_RST 4
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

/********************** Dissolved Oxygen Probe SoftwareSerial Pins **********************/

#define DO_RX 14
#define DO_TX 16
SoftwareSerial doSerial(DO_RX, DO_TX);

/************************* WiFi Access Point *********************************/

// Optionally configure a GPRS APN, username, and password.
// You might need to do this to access your network's GPRS/data
// network.  Contact your provider for the exact APN, username,
// and password values.  Username and password are optional and
// can be removed, but APN is required.
#define FONA_APN       ""
#define FONA_USERNAME  ""
#define FONA_PASSWORD  ""

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    ""
#define AIO_KEY         ""

/************ Global State (you don't need to change this!) ******************/

// Setup the FONA MQTT class by passing in the FONA class and MQTT server and login details.
Adafruit_MQTT_FONA mqtt(&fona, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// You don't need to change anything below this line!
#define halt(s) { Serial.println(F( s )); while(1);  }

// FONAconnect is a helper function that sets up the FONA and connects to
// the GPRS network. See the fonahelper.cpp tab above for the source!
boolean FONAconnect(const __FlashStringHelper *apn, const __FlashStringHelper *username, const __FlashStringHelper *password);
void setFonaPowerDownMode(void);
void setFonaWakeUpMode(void);
void checkForNetwork(void);

/****************************** Feeds ***************************************/

// Setup feeds on AdafruitIO for publishing
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish bkny_water_temp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-temp");
Adafruit_MQTT_Publish bkny_water_ec = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-ec");
Adafruit_MQTT_Publish bkny_water_tds = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-tds");
Adafruit_MQTT_Publish bkny_water_sal = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-sal");
Adafruit_MQTT_Publish bkny_water_sg = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-sg");
Adafruit_MQTT_Publish bkny_water_do = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bkny-water-do");

/*************************** Sketch Code ************************************/

String ecSensorString;                         // A string to hold the data from the EC probe
String doSensorString;


// Temperature probe pins
int sensorPowerPin = 11;                            // GPIO pin that powers water temp sensor before measurements
int sensorDataPin = A0;                             // ADC pin that receives water temp data

int dtrPin = 5;                                     // Toggles FONA sleep

// How many transmission failures in a row we're willing to be ok with before reset?
uint8_t txfailures = 0;
#define MAXTXFAILURES 3

void setup() {
  pinMode(sensorPowerPin, OUTPUT);
  pinMode(dtrPin, OUTPUT);
  analogReference(DEFAULT);           // Set AREF to 3.3V

  Watchdog.enable(8000);              // Enable watchdog timer for 8 second reset

  // Start EC probe hardware serial and put EC probe to sleep
  Serial1.begin(38400);
  Serial1.print('\r');
  delay(100);
  Serial1.print("sleep");
  Serial1.print('\r');

  // Start DO probe software serial and put DO probe to sleep.
  doSerial.begin(9600);
  doSerial.print('\r');
  doSerial.print("sleep");
  doSerial.print('\r');

  ecSensorString.reserve(30);         // Set aside some bytes for receiving data from the EC probe
  doSensorString.reserve(30);

  // Initialize the FONA module
  while (! FONAconnect(F(FONA_APN), F(FONA_USERNAME), F(FONA_PASSWORD))) {
    // Serial.println("Retrying FONA");
  }
  // Serial.println(F("Connected to Cellular!"));

  Watchdog.reset();
  delay(5000);                        // Wait 5 seconds to stabilize connection
  Watchdog.reset();
}

void loop() {
  ecSensorString = "";                         // A string to hold the data from the EC probe
  doSensorString = "";

  // Temperature probe measurement value
  float temp_val = 0;                                     

  // Electrical conductivity probe definitions
  float ec_val = 0;                                       // Floating values to be transmitted via MQTT
  float tds_val = 0;
  float sal_val = 0;
  float sg_val = 0;

  // Dissolved oxygen probe definitions
  float do_val = 0;

  Watchdog.reset();

  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).
  MQTT_connect();
  delay(50);

  temp_val = read_temp();                                // Take measurement from temperature probe

  read_ec(temp_val, ec_val, tds_val, sal_val, sg_val, ecSensorString);   // Take measurement from EC probe

  do_val = read_do(temp_val, ec_val);                   // Take measurement from DO probe

  // Publish measured values to AdafruitIO feeds via MQTT packets
  publish_value("temp", temp_val, bkny_water_temp);
  publish_value("ec", ec_val, bkny_water_ec);
  publish_value("tds", tds_val, bkny_water_tds);
  publish_value("sal", sal_val, bkny_water_sal);
  publish_value("sg", sg_val, bkny_water_sg);
  publish_value("do", do_val, bkny_water_do);

  // Disconnect MQTT connection
  MQTT_disconnect();

  // Power down FONA
  setFonaPowerDownMode();

  // Put microcontroller to sleep
  int number_of_sleeper_loops = 225; //time between taking a reading is 75 * 8 seconds = 600 seconds.
  for (int i = 0; i < number_of_sleeper_loops; i++) {
    Watchdog.sleep(8000);
  }

  // Wake up FONA
  setFonaWakeUpMode();
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void MQTT_connect() {
  int8_t ret;
  int mqtt_count = 0;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  //Serial.print("Connecting to MQTT... ");

  while (((ret = mqtt.connect()) != 0) && (mqtt_count < 5)) { // connect will return 0 for connected
    Watchdog.reset();
    //Serial.println(mqtt.connectErrorString(ret));
    //Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    Watchdog.sleep(5000);
    mqtt_count += 1;
    if (mqtt_count == 5) {
      checkForNetwork();
      mqtt_count = 0;
    }
  }
  //Serial.println("MQTT connected!");
}


//
void MQTT_disconnect() {
  int8_t ret;
  //Serial.println("Disconnecting from MQTT...");
  while ((ret = mqtt.disconnect()) != 0) {
    Watchdog.reset();
    //Serial.println("Retrying MQTT disconnection in 5 seconds...");
    Watchdog.sleep(5000);
  }
  //Serial.println("MQTT disconnected!");
}


// Read temperature from temperature probe
float read_temp(void) {
  float v_out = 0;                                // Voltage output from temp sensor
  float temp;                                     // Holds final temperature value
  digitalWrite(sensorDataPin, LOW);               // Set pull-up on analog input pin
  digitalWrite(sensorPowerPin, HIGH);             // Power-on sensor
  for (int i = 0; i < 20; i++) {
    delay(1);                                     // Wait 1 ms for sensor to stabilize
    v_out += analogRead(0);                       // Read the input pin and add to multiple measurement aggregate
  }
  v_out /= 20;                                    // Divide by # of measurements to find average value. Done to smooth noisy measurements
  digitalWrite(sensorPowerPin, LOW);              // Power-off sensor
  v_out *= .0032;                                 // Convert ADC points to volts (3.3/1023 because AREF is set to 3.3V)
  v_out *= 1000;                                  // Convert volts to millivolts
  temp = 0.0512 * v_out - 20.5128;                // Convert millivolts to Celsius
  return temp;
}


// Read electrical conductivity, total dissolved solids, salinity, and specific gravity from EC probe
void read_ec(float temp, float& ec, float& tds, float& sal, float& sg, String& ec_string) {
  // Definitions for EC probe data
  char ec_array[30];         // Char array
  char *EC;                  // Char pointers
  char *TDS;
  char *SAL;
  char *GRAV;

  // Wake up EC probe
  Serial1.print('\r');
  delay(100);

  // Send temperature compensation value to EC probe
  Serial1.print("t,");
  Serial1.print(String(temp));
  Serial1.print('\r');
  delay(10);

  // Take measurement on EC probe
  Serial1.print('r');
  Serial1.print('\r');
  delay(1000);                      // Measurements take 1s

  // Receive response from EC probe
  while (Serial1.available() > 0) {
    ec_string = Serial1.readStringUntil(13);                   // Read the string until <CR>
    //Serial.print("Unused EC Sensor String: ");
    //Serial.println(ec_string);
    if (isdigit(ec_string[0])) {
      ec_string.toCharArray(ec_array, 30);    // Convert the string to a char array
      EC = strtok(ec_array, ",");                       // Parse the array at each comma
      TDS = strtok(NULL, ",");
      SAL = strtok(NULL, ",");
      GRAV = strtok(NULL, ",");
      ec = atof(EC);                                            // Convert to floats
      tds = atof(TDS);
      sal = atof(SAL);
      sg = atof(GRAV);

      //Serial.print("EC Sensor String: ");
      //Serial.println(ec_string);
    }
  }
  
  // Send EC probe to sleep
  Serial1.print("sleep");
  Serial1.print('\r');

  Watchdog.reset();
}


// Read dissolved oxygen from DO probe
float read_do(float temp, float ec) {
  float do_val;
  boolean do_string_complete = false;
  doSensorString = "";
  // End FONA SoftwareSerial and begin DO probe SoftwareSerial
  fonaSS.end();
  doSerial.begin(9600);
  //while (!doSerial) {}
  
  // Wake up DO probe
  doSerial.print('\r');
  delay(10);

  // Send compensation values to DO probe
  doSerial.print("t,");
  doSerial.print(String(temp));
  doSerial.print('\r');
  delay(10);
  
  doSerial.print("s,");
  doSerial.print(String(ec));
  doSerial.print('\r');
  delay(10);

  // Take measurement on DO probe
  doSerial.print('r');
  doSerial.print('\r');
  delay(1000);

  Watchdog.reset();

  //Serial.print("DO chars: ");
  // Receive response from DO probe
  while (doSerial.available() > 0) {
    char inchar = (char)doSerial.read();
    //Serial.print(inchar);
    if ((do_string_complete == false) && ((isdigit(inchar) || (inchar == '.')))) {               // Add to string if character is a digit or decimal point
      doSensorString += inchar;
    }
    if ((do_string_complete == false) && isdigit(doSensorString[0]) && (inchar == '\r')) {       // Build the string until <CR>
      //Serial.println();
      do_string_complete = true;
      do_val = doSensorString.toFloat();
      //Serial.print("DO Sensor String: ");
      //Serial.println(doSensorString);
    }
  }

  // Send probe to sleep and switch back to FONA SoftwareSerial
  doSerial.print("sleep");
  doSerial.print('\r');
  delay(10);

  String bufferstring = "";
  while (doSerial.available() > 0) {
    char inchar = (char)doSerial.read();
    bufferstring += inchar;
  }
  //Serial.print("Loop DO sleep: ");
  //Serial.println(bufferstring);

  doSerial.end();
  fonaSS.begin(4800);

  Watchdog.reset();
  
  return do_val;
}

void publish_value(String sensor_type, float sensor_val, Adafruit_MQTT_Publish &feed_name){
  //Serial.print(F("\nSending "));
  //Serial.print(sensor_type);
  //Serial.print(" value to AdafruitIO: ");
  //Serial.println(sensor_val);
  if (! feed_name.publish(sensor_val)) {
    //Serial.println(F("Failed"));
    txfailures++;
  } else {
    //Serial.println(F("OK!"));
    txfailures = 0;
  }
  Watchdog.reset();
}

/***************************************************
  Adafruit MQTT Library FONA Example

  Designed specifically to work with the Adafruit FONA
  ----> http://www.adafruit.com/products/1946
  ----> http://www.adafruit.com/products/1963
  ----> http://www.adafruit.com/products/2468
  ----> http://www.adafruit.com/products/2542

  These cellular modules use TTL Serial to communicate, 2 pins are
  required to interface.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/
