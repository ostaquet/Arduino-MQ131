/******************************************************************************
 * Arduino-MQ131-driver                                                       *
 * --------------------                                                       *
 * Arduino driver for gas sensor MQ131 (O3)                                   *
 * Author: Olivier Staquet                                                    *
 * Last version available on https://github.com/ostaquet/Arduino-MQ131-driver *
 ******************************************************************************
 * MIT License
 *
 * Copyright (c) 2018 Olivier Staquet
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************/

#include "MQ131.h"

/**
 * Constructor, nothing special to do
 */
MQ131Class::MQ131Class(uint32_t _RL) {
  valueRL = _RL;
}

/**
 * Destructor, nothing special to do
 */
MQ131Class::~MQ131Class() {
}

/**
 * Init core variables
 */
 void MQ131Class::begin(uint8_t _pinPower, uint8_t _pinSensor, MQ131Model _model, uint32_t _RL, Stream* _debugStream) { 
  // Define if debug is requested
  enableDebug = _debugStream != NULL;
  debugStream = _debugStream;
  
 	// Setup the model
 	model = _model;

 	// Store the circuit info (pin and load resistance)
 	pinPower = _pinPower;
 	pinSensor = _pinSensor;
 	valueRL = _RL;

  // Setup default calibration value
  switch(model) {
    case LOW_CONCENTRATION :
      setR0(MQ131_DEFAULT_LO_CONCENTRATION_R0);
      setTimeToRead(MQ131_DEFAULT_LO_CONCENTRATION_TIME2READ);
      break;
    case ETC_CONCENTRATION :
      setR0(MQ131_DEFAULT_ETC_CONCENTRATION_R0);
      setTimeToRead(MQ131_DEFAULT_LH_CONCENTRATION_TIME2READ);
      break;
    case HIGH_CONCENTRATION :
      setR0(MQ131_DEFAULT_HI_CONCENTRATION_R0);
      setTimeToRead(MQ131_DEFAULT_HI_CONCENTRATION_TIME2READ);
      break;
  }

 	// Setup pin mode
 	pinMode(pinPower, OUTPUT);
 	pinMode(pinSensor, INPUT);

  // Switch off the heater as default status
  digitalWrite(pinPower, LOW);
 }

/**
 * Do a full cycle (heater, reading, stop heater)
 * The function gives back the hand only at the end
 * of the read cycle!
 */
 void MQ131Class::sample() {
 	startHeater();
 	while(!isTimeToRead()) {
 		delay(1000);
 	}
 	lastValueRs = readRs();
 	stopHeater();
 }

/**
 * Start the heater
 */
 void MQ131Class::startHeater() {
 	digitalWrite(pinPower, HIGH);
 	secLastStart = millis()/1000;
 }

/**
 * Check if it is the right time to read the Rs value
 */
 bool MQ131Class::isTimeToRead() {
 	// Check if the heater has been started...
 	if(secLastStart < 0) {
 		return false;
 	}
 	// OK, check if it's the time to read based on calibration parameters
 	if(millis() / 1000 >= secLastStart + getTimeToRead()) {
 		return true;
 	}
 	return false;
 } 

/**
 * Stop the heater
 */
 void MQ131Class::stopHeater() {
 	digitalWrite(pinPower, LOW);
 	secLastStart = -1;
 }

/**
 * Get parameter time to read
 */
 long MQ131Class::getTimeToRead() {
 	return secToRead;
 }

/**
 * Set parameter time to read (for calibration or to recall
 * calibration from previous run)
 */
 void MQ131Class::setTimeToRead(uint32_t sec) {
 	secToRead = sec;
 }

/**
 * Read Rs value
 */
 float MQ131Class::readRs() {
 	// Read the value
 	uint16_t valueSensor = analogRead(pinSensor);
 	// Compute the voltage on load resistance (for 5V Arduino)
 	float vRL = ((float)valueSensor) / 1024.0 * 5.0;
 	// Compute the resistance of the sensor (for 5V Arduino)
 	float rS = (5.0 / vRL - 1.0) * valueRL;
 	return rS;
 }

/**
 * Set environmental values
 */
 void MQ131Class::setEnv(int8_t tempCels, uint8_t humPc) {
 	temperatureCelsuis = tempCels;
 	humidityPercent = humPc;
 }

/**
 * Get correction to apply on Rs depending on environmental
 * conditions
 */
 float MQ131Class::getEnvCorrectRatio() {
 	// Select the right equation based on humidity
  // Extract/calc the ratios for diferent Humidity
   float Hratio30 = -0.0141 * temperatureCelsuis + 1.5623; // R^2 = 0.9986
   float Hratio60 = -0.0119 * temperatureCelsuis + 1.3261; // R^2 = 0.9976
   float Hratio85 = -0.0103 * temperatureCelsuis + 1.1507; // R^2 = 0.996
  
   // If default value, ignore correction ratio
 	if(humidityPercent == 60 && temperatureCelsuis == 20) {
 		return 1.06;
 	}
 	// For humidity > 60% ratio Average between 60-85 curves
 	if(humidityPercent > 60) {
 		    
 		return Hratio60 + (Hratio85 - Hratio60) * (humidityPercent - 60) / (85 - 60);
 	}
  	
 	// Humidity < 60%, ratio Average between 30-60 curves
 	
 	return Hratio30 + (Hratio60 - Hratio30) * (humidityPercent - 30) / (60 - 30);
 }

 /**
 * Get gas concentration for O3 in ppm
 */
 float MQ131Class::getO3(MQ131Unit unit) {
 	// If no value Rs read, return 0.0
 	if(lastValueRs < 0) {
 		return 0.0;
 	}

  float ratio = 0.0;

 	switch(model) {
 		case LOW_CONCENTRATION :
 			// Use the equation to compute the O3 concentration in ppm
 			// R^2 = 0.9987
      // Compute the ratio Rs/R0 and apply the environmental correction
      ratio = lastValueRs / valueR0 * getEnvCorrectRatio();
      return convert(9.4783 * pow(ratio, 2.3348), PPB, unit);
     case ETC_CONCENTRATION :
 			// Use the equation to compute the O3 concentration in ppm
 			// R^2 = 0.99
      // Compute the ratio Rs/R0 and apply the environmental correction
      ratio = lastValueRs / valueR0 * getEnvCorrectRatio();
      return convert(23.8887 * pow(ratio, 1.1101), PPB, unit);
 		case HIGH_CONCENTRATION :
 			// Use the equation to compute the O3 concentration in ppm
 			// R^2 = 0.99
      // Compute the ratio Rs/R0 and apply the environmental correction
      ratio = lastValueRs / valueR0 * getEnvCorrectRatio();
      return convert(8.1399 * pow(ratio, 2.3297), PPM, unit);
 		default :
 			return 0.0;
  }
}

 /**
  * Convert gas unit of gas concentration
  */
 float MQ131Class::convert(float input, MQ131Unit unitIn, MQ131Unit unitOut) {
  if(unitIn == unitOut) {
    return input;
  }

  float concentration = 0;

  switch(unitOut) {
    case PPM :
      // We assume that the unit IN is PPB as the sensor provide only in PPB and PPM
      // depending on the type of sensor (METAL or BLACK_BAKELITE)
      // So, convert PPB to PPM
      return input / 1000.0;
    case PPB :
      // We assume that the unit IN is PPM as the sensor provide only in PPB and PPM
      // depending on the type of sensor (METAL or BLACK_BAKELITE)
      // So, convert PPM to PPB
      return input * 1000.0;
    case MG_M3 :
      if(unitIn == PPM) {
        concentration = input;
      } else {
        concentration = input / 1000.0;
      }
      return concentration * 48.0 / 22.71108;
    case UG_M3 :
      if(unitIn == PPB) {
        concentration = input;
      } else {
        concentration = input * 1000.0;
      }
      return concentration * 48.0 / 22.71108;
    default :
      return input;
  }
}

 /**
  * Calibrate the basic values (R0 and time to read)
  */
void MQ131Class::calibrate() {
  // Take care of the last Rs value read on the sensor
  // (forget the decimals)
  float lastRsValue = 0;
  // Count how many time we keep the same Rs value in a row
  uint8_t countReadInRow = 0;
  // Count how long we have to wait to have consistent value
  uint8_t count = 0;

  // Get some info
  if(enableDebug) {
    debugStream->println(F("MQ131 : Starting calibration..."));
    debugStream->println(F("MQ131 : Enable heater"));
    debugStream->print(F("MQ131 : Stable cycles required : "));
    debugStream->print(MQ131_DEFAULT_STABLE_CYCLE);
    debugStream->println(F(" (compilation parameter MQ131_DEFAULT_STABLE_CYCLE)"));
  }

  // Start heater
  startHeater();

  uint8_t timeToReadConsistency = MQ131_DEFAULT_STABLE_CYCLE;

  while(countReadInRow <= timeToReadConsistency) {
    float value = readRs();

    if(enableDebug) {
      debugStream->print(F("MQ131 : Rs read = "));
      debugStream->print((uint32_t)value);
      debugStream->println(F(" Ohms"));
    }
    
    if((uint32_t)lastRsValue != (uint32_t)value) {
      lastRsValue = value;
      countReadInRow = 0;
    } else {
      countReadInRow++;
    }
    count++;
    delay(1000);
  }

  if(enableDebug) {
    debugStream->print(F("MQ131 : Stabilisation after "));
    debugStream->print(count);
    debugStream->println(F(" seconds"));
    debugStream->println(F("MQ131 : Stop heater and store calibration parameters"));
  }

  // Stop heater
  stopHeater();

  // We have our R0 and our time to read
  setR0(lastRsValue);
  setTimeToRead(count);
}

 /**
  * Store R0 value (come from calibration or set by user)
  */
  void MQ131Class::setR0(float _valueR0) {
  	valueR0 = _valueR0;
  }

 /**
 * Get R0 value
 */
 float MQ131Class::getR0() {
 	return valueR0;
 }

MQ131Class MQ131(MQ131_DEFAULT_RL);
