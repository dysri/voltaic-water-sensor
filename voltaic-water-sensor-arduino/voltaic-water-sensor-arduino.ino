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
  Modified by Dylan Sri-Jayantha for Voltaic Systems
 ****************************************************/
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
#define AIO_USERNAME    "dylansri"
#define AIO_KEY         "072eb42ef5764740ac0723f2c598423d"

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

// Setup a feed called 'er_water_temp' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish er_water_temp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/er_water_temp");

/*************************** Sketch Code ************************************/

// How many transmission failures in a row we're willing to be ok with before reset
uint8_t txfailures = 0;
#define MAXTXFAILURES 3

float temp; // Where the final temperature data is stored
int sensorPowerPin = 11; // GPIO pin that powers water temp sensor before measurements
int sensorDataPin = A0; // ADC pin that receives water temp data
int dtrPin = 5; // Toggles FONA sleep
uint32_t temp_value;

void setup() {
  pinMode(sensorPowerPin, OUTPUT);
  pinMode(dtrPin, OUTPUT);
  analogReference(DEFAULT); // Set AREF to 3.3V
  
  Watchdog.enable(8000); // Enable watchdog timer for 8 second reset
  
  Serial.begin(115200);
  
  // Initialise the FONA module
  while (! FONAconnect(F(FONA_APN), F(FONA_USERNAME), F(FONA_PASSWORD))) {
    Serial.println("Retrying FONA");
  }

  Serial.println(F("Connected to Cellular!"));

  Watchdog.reset();
  delay(5000);  // wait a few seconds to stabilize connection
  Watchdog.reset();
}

void loop() {

  Watchdog.reset();

  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).
  MQTT_connect();

  Watchdog.reset();

  // Retrieve battery voltage and charge percentage, print to serial monitor. (For debugging)
//  printFonaBatteryStatus();

  // Call the function “read_temp” and return the temperature in C°
  temp_value = read_temp();
  
  // Publish measured values
  Serial.print(F("\nSending water temp value: "));
  Serial.print(temp_value);
  Serial.println("...");
  if (! er_water_temp.publish(temp_value)) {
    Serial.println(F("Failed"));
    txfailures++;
  } else {
    Serial.println(F("OK!"));
    txfailures = 0;
  }

  Watchdog.reset();  

  // Disconnect MQTT connection
  MQTT_disconnect();
  
  // Power down FONA
  setFonaPowerDownMode();
  
  // Put microcontroller to sleep
  int number_of_sleeper_loops = 110; //time between taking a reading is 150 * 8 seconds = 1200 seconds.
  for (int i = 0; i < number_of_sleeper_loops; i++) {
    Watchdog.sleep(8000);
  }

  // Wake up FONA
  setFonaWakeUpMode();

  // Check for active network. Sleep for 1 hr and reset if network is not detected
  checkForNetwork();
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
  }
  Serial.println("MQTT connected!");
}

void MQTT_disconnect() {
  int8_t ret;
  Serial.println("Disconnecting from MQTT...");
  while((ret = mqtt.disconnect()) != 0) {
    Serial.println("Retrying MQTT disconnection in 5 seconds...");
    delay(5000);
  }
  Serial.println("MQTT disconnected!");
}

float read_temp(void){   // The read temperature function
  float v_out = 0;             // Voltage output from temp sensor 
  float temp;              // The final temperature is stored here
  digitalWrite(sensorDataPin, LOW);   // Set pull-up on analog pin
  digitalWrite(sensorPowerPin, HIGH);   // Set pin 11 high, this will turn on temp sensor
  for (int i = 0; i < 20; i++) {
    delay(1);                // Wait 1 ms for sensor to stabilize
    v_out += analogRead(0); // Read the input pin and add to multiple measurement aggregate
  }
  v_out /= 20;   // Divide by # of measurements to find average value. Done to prevent noisy measurements
  digitalWrite(sensorPowerPin, LOW);    // Set pin 11 low, this will turn off temp sensor
  v_out*=.0032;           // Convert ADC points to volts (we are using 3.3/1023 because AREF is set to 3.3V)
  v_out*=1000;             // Convert volts to millivolts
  temp= 0.0512 * v_out -20.5128; // The equation from millivolts to temperature
  return temp;             // Send back the temp
}
