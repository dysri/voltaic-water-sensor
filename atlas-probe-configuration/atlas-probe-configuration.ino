#include <SoftwareSerial.h>

#define rx 14
#define tx 16
SoftwareSerial doSerial(rx, tx);                      // define software serial port for DO data transfer

String inputstring = "";
boolean input_string_complete = false;

String ecSensorString = "";                             //a string to hold the data from the EC probe
boolean ec_sensor_string_complete = false;               //have we received all the data from the EC probe
float ec_val;

String doSensorString = "";                             //a string to hold the data from the DO probe
boolean do_sensor_string_complete = false;               //have we received all the data from the DO probe
float do_val;

float temp_val; // Where the final temperature data is stored
int sensorPowerPin = 11; // GPIO pin that powers water temp sensor before measurements
int sensorDataPin = A0; // ADC pin that receives water temp data

void setup() {
  Serial.begin(9600);                                 //set baud rate for the USB serial to 9600
  Serial1.begin(38400);
  doSerial.begin(9600);
  inputstring.reserve(10);
  doSensorString.reserve(30);                            //set aside some bytes for receiving data from the PC
  ecSensorString.reserve(30);                           //set aside some bytes for receiving data from Atlas Scientific product

  pinMode(sensorPowerPin, OUTPUT);
  analogReference(DEFAULT); // Set AREF to 3.3V
}


void loop() {
  
  delay(2000);
  if (Serial.available() > 0) {
    inputstring = Serial.readStringUntil(13);           //read the string until we see a <CR>
    input_string_complete = true;                       //set the flag used to tell if we have received a completed string from the PC
  }

  if (input_string_complete == true) {                //if a string from the PC has been received in its entirety
    Serial1.print(inputstring);                       //send that string to the Atlas Scientific product
    Serial1.print('\r');
  }

  temp_val = read_temp();
  Serial.print("Temperature: ");
  Serial.println(String(temp_val));

  Serial1.print("t,");
  Serial1.print(String(temp_val));
  Serial1.print('\r');

  Serial1.print('r');
  Serial1.print('\r');
  delay(1000);

  while (Serial1.available() > 0) {
    ecSensorString = Serial1.readStringUntil(13);         //read the string until we see a <CR>
    Serial.print("Buffered EC string: ");
    Serial.println(ecSensorString);
    if (isdigit(ecSensorString[0])) {
      ec_sensor_string_complete = true;                      //set the flag used to tell if we have received a completed string from the PC
      print_EC_data();
    }
  }

  doSerial.print("t,");
  doSerial.print(String(temp_val));
  doSerial.print('\r'); 
  delay(10);

  doSerial.print("s,");
  doSerial.print(String(ec_val));
  doSerial.print('\r');
  delay(10);

  doSerial.print('r');
  doSerial.print('\r');
  delay(1000);
  
  if (input_string_complete == true) {                //if a string from the PC has been received in its entirety
    doSerial.print(inputstring);                       //send that string to the Atlas Scientific product
    doSerial.print('\r');
  }

  Serial.print("DO buffer: ");
  while (doSerial.available() > 0) {                     //if we see that the Atlas Scientific product has sent a character
    char inchar = (char)doSerial.read();              //get the char we just received
    Serial.print(inchar);
    if ((do_sensor_string_complete == false) && ((isdigit(inchar) || (inchar == '.')))) {
      doSensorString += inchar;
    }
    if ((do_sensor_string_complete == false) && isdigit(doSensorString[0]) && (inchar == '\r')) {                             //if the incoming character is a <CR>
      do_sensor_string_complete = true;                  //set the flag
    }
  }
  Serial.println();

  if ((do_sensor_string_complete == true) && (isdigit(doSensorString[0]))) {                //if a string from the Atlas Scientific product has been received in its entirety
    Serial.print("DO value: ");
    Serial.println(doSensorString);
    do_val = doSensorString.toFloat();
  }

  doSerial.print("sleep");
  doSerial.print('\r');

  inputstring = "";
  input_string_complete = false;
  doSensorString = "";
  do_sensor_string_complete = false;
  ecSensorString = "";
  ec_sensor_string_complete = false;
}

void print_EC_data(void) {                            //this function will pars the string

  char ecSensorString_array[30];                        //we make a char array
  char *EC;                                           //char pointer used in string parsing
  char *TDS;                                          //char pointer used in string parsing
  char *SAL;                                          //char pointer used in string parsing
  char *GRAV;                                         //char pointer used in string parsing
  float ec_val;                                         //used to hold a floating point number that is the EC
  float tds_val;
  float sal_val;
  float sg_val;

  ecSensorString.toCharArray(ecSensorString_array, 30);   //convert the string to a char array
  EC = strtok(ecSensorString_array, ",");               //let's parse the array at each comma
  TDS = strtok(NULL, ",");                            //let's parse the array at each comma
  SAL = strtok(NULL, ",");                            //let's parse the array at each comma
  GRAV = strtok(NULL, ",");                           //let's parse the array at each comma
  ec_val = atof(EC);
  tds_val = atof(TDS);
  sal_val = atof(SAL);
  sg_val = atof(GRAV);

  Serial.print("Conductivity: ");
  Serial.println(EC);
  Serial.print("TDS: ");
  Serial.println(TDS);
  Serial.print("Salinity: ");
  Serial.println(SAL);
  Serial.print("SG: ");
  Serial.println(GRAV);
}

float read_temp(void) {  // The read temperature function
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
  v_out *= .0032;         // Convert ADC points to volts (we are using 3.3/1023 because AREF is set to 3.3V)
  v_out *= 1000;           // Convert volts to millivolts
  temp = 0.0512 * v_out - 20.5128; // The equation from millivolts to temperature
  return temp;             // Send back the temp
}

