#include "HX711.h"
#include "SPIFFS.h"

// Shared clock pin for all HX711 modules
#define SHARED_HX711_SCK 4

// Define individual data pins for each HX711 load cell
#define LOADCELL_DOUT_PIN0 32
#define LOADCELL_DOUT_PIN1 12
#define LOADCELL_DOUT_PIN2 13
#define LOADCELL_DOUT_PIN3 15

// Create one HX711 instance per load cell
HX711 scale0;
HX711 scale1;
HX711 scale2;
HX711 scale3;

// Calibration factors (default values; updated via calibration)
float calibrationFactor0 = 2280.0;
float calibrationFactor1 = 2280.0;
float calibrationFactor2 = 2280.0;
float calibrationFactor3 = 2280.0;

// Baseline offsets (used to reset the no-load baseline, e.g., to counter drift)
float offset0 = 0.0;
float offset1 = 0.0;
float offset2 = 0.0;
float offset3 = 0.0;

// Threshold for detecting a “pressed” load cell (adjust as needed)
#define PRESS_THRESHOLD 0.5

// Filtering configuration: use the last 5 samples to filter out transient spikes
#define NUM_SAMPLES 5
float lastReadings0[NUM_SAMPLES] = {0};
float lastReadings1[NUM_SAMPLES] = {0};
float lastReadings2[NUM_SAMPLES] = {0};
float lastReadings3[NUM_SAMPLES] = {0};
int sampleIndex0 = 0, sampleCount0 = 0;
int sampleIndex1 = 0, sampleCount1 = 0;
int sampleIndex2 = 0, sampleCount2 = 0;
int sampleIndex3 = 0, sampleCount3 = 0;

// Function prototypes
void calibrate();
float getStableReading(int cell, const char* phase);
float getWeightInput(String prompt);
void saveCalibrationToSPIFFS();
void loadCalibrationFromSPIFFS();
float filterSpike(int cell, float newVal);
void resetDrift();
void clearSPIFFSValues();

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for serial connection

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  
  // Load saved calibration factors and offsets if available
  loadCalibrationFromSPIFFS();

  // Initialize each HX711 module with its dedicated data pin and shared clock pin
  scale0.begin(LOADCELL_DOUT_PIN0, SHARED_HX711_SCK);
  scale1.begin(LOADCELL_DOUT_PIN1, SHARED_HX711_SCK);
  scale2.begin(LOADCELL_DOUT_PIN2, SHARED_HX711_SCK);
  scale3.begin(LOADCELL_DOUT_PIN3, SHARED_HX711_SCK);
  
  // Set gain for each HX711 module (typically 128 for channel A)
  scale0.set_gain(128);
  scale1.set_gain(128);
  scale2.set_gain(128);
  scale3.set_gain(128);
  
  // Tare each scale to zero out the baseline (no load condition)
  scale0.tare();
  scale1.tare();
  scale2.tare();
  scale3.tare();
  
  // Set the calibration factors for each scale
  scale0.set_scale(calibrationFactor0);
  scale1.set_scale(calibrationFactor1);
  scale2.set_scale(calibrationFactor2);
  scale3.set_scale(calibrationFactor3);
  
  // Print header for the Serial Plotter and instructions
  Serial.println("Scale0,Scale1,Scale2,Scale3");
  Serial.println("Type /calibrate to calibrate a load cell.");
  Serial.println("Type /drift to reset the baseline due to drift.");
  Serial.println("Type /clear to clear SPIFFS calibration data.");
}

void loop() {
  // Check for Serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.equalsIgnoreCase("/calibrate")) {
      calibrate();
    } else if (command.equalsIgnoreCase("/drift")) {
      resetDrift();
    } else if (command.equalsIgnoreCase("/clear")) {
      clearSPIFFSValues();
    }
  }
  
  // Normal operation: read calibrated weight, subtract baseline offset, then filter spikes.
  float weight0 = scale0.get_units() - offset0;
  float weight1 = scale1.get_units() - offset1;
  float weight2 = scale2.get_units() - offset2;
  float weight3 = scale3.get_units() - offset3;
  
  weight0 = filterSpike(0, weight0);
  weight1 = filterSpike(1, weight1);
  weight2 = filterSpike(2, weight2);
  weight3 = filterSpike(3, weight3);
  
  // Print the filtered readings as CSV for the Serial Plotter
  Serial.print(weight0, 2);
  Serial.print(",");
  Serial.print(weight1, 2);
  Serial.print(",");
  Serial.print(weight2, 2);
  Serial.print(",");
  Serial.println(weight3, 2);
  
  delay(15);
}

//
// calibrate() - Guides the user through calibrating a selected load cell.
void calibrate() {
  Serial.println("Entering calibration mode.");
  Serial.println("Apply pressure on one load cell to select it for calibration.");
  
  int selectedCell = -1;
  while (selectedCell < 0) {
    float w0 = scale0.get_units();
    float w1 = scale1.get_units();
    float w2 = scale2.get_units();
    float w3 = scale3.get_units();
    
    if (w0 > PRESS_THRESHOLD) {
      selectedCell = 0;
    } else if (w1 > PRESS_THRESHOLD) {
      selectedCell = 1;
    } else if (w2 > PRESS_THRESHOLD) {
      selectedCell = 2;
    } else if (w3 > PRESS_THRESHOLD) {
      selectedCell = 3;
    }
    delay(100);
  }
  
  Serial.print("Load cell ");
  Serial.print(selectedCell);
  Serial.println(" detected. Do you want to calibrate this cell? (Y/N)");
  
  while (true) {
    while (!Serial.available()) { delay(100); }
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("Y") || input.equalsIgnoreCase("YES")) {
      break;
    } else {
      Serial.println("Calibration cancelled.");
      return;
    }
  }
  
  Serial.println("Starting calibration for selected load cell.");
  Serial.println("Step 1: Baseline measurement (no load). Remove all force from the load cell.");
  float baseline = getStableReading(selectedCell, "baseline");
  
  Serial.println("Step 2: First calibration measurement.");
  Serial.println("Apply a known force to the load cell and press Enter when ready.");
  float reading1 = getStableReading(selectedCell, "first calibration");
  float weight1 = getWeightInput("Enter the known weight (in kg) for the first calibration measurement:");
  
  Serial.println("Step 3: Second calibration measurement.");
  Serial.println("Apply a different known force to the load cell and press Enter when ready.");
  float reading2 = getStableReading(selectedCell, "second calibration");
  float weight2 = getWeightInput("Enter the known weight (in kg) for the second calibration measurement:");
  
  float calFactor1 = (reading1 - baseline) / weight1;
  float calFactor2 = (reading2 - baseline) / weight2;
  float newCalFactor = (calFactor1 + calFactor2) / 2.0;
  
  switch (selectedCell) {
    case 0:
      calibrationFactor0 = newCalFactor;
      scale0.set_scale(newCalFactor);
      break;
    case 1:
      calibrationFactor1 = newCalFactor;
      scale1.set_scale(newCalFactor);
      break;
    case 2:
      calibrationFactor2 = newCalFactor;
      scale2.set_scale(newCalFactor);
      break;
    case 3:
      calibrationFactor3 = newCalFactor;
      scale3.set_scale(newCalFactor);
      break;
  }
  
  Serial.print("New calibration factor for load cell ");
  Serial.print(selectedCell);
  Serial.print(": ");
  Serial.println(newCalFactor);
  
  saveCalibrationToSPIFFS();
  Serial.println("Calibration data saved to SPIFFS.");
  Serial.println("Exiting calibration mode.");
}

//
// resetDrift() - Resets the baseline offsets using the current readings (to correct for drift).
void resetDrift() {
  Serial.println("Drift correction: Set current readings as new baseline.");
  Serial.println("Make sure no force is applied then press Enter.");
  while (!Serial.available()) { delay(100); }
  String dummy = Serial.readStringUntil('\n');
  
  float sum0 = 0, sum1 = 0, sum2 = 0, sum3 = 0;
  float min0 = 1e9, min1 = 1e9, min2 = 1e9, min3 = 1e9;
  float max0 = -1e9, max1 = -1e9, max2 = -1e9, max3 = -1e9;
  for (int i = 0; i < 50; i++) {
    float r0 = scale0.get_units();
    float r1 = scale1.get_units();
    float r2 = scale2.get_units();
    float r3 = scale3.get_units();
    
    sum0 += r0;
    sum1 += r1;
    sum2 += r2;
    sum3 += r3;
    
    if (r0 < min0) min0 = r0;
    if (r1 < min1) min1 = r1;
    if (r2 < min2) min2 = r2;
    if (r3 < min3) min3 = r3;
    
    if (r0 > max0) max0 = r0;
    if (r1 > max1) max1 = r1;
    if (r2 > max2) max2 = r2;
    if (r3 > max3) max3 = r3;
    
    delay(15);
  }
  if ((max0 - min0) > 5 || (max1 - min1) > 5 || (max2 - min2) > 5 || (max3 - min3) > 5) {
    Serial.println("Unstable readings detected during drift correction. Please try again.");
    return;
  }
  
  offset0 = sum0 / 50.0;
  offset1 = sum1 / 50.0;
  offset2 = sum2 / 50.0;
  offset3 = sum3 / 50.0;
  
  Serial.println("New baseline offsets set:");
  Serial.print("Offset0: "); Serial.println(offset0);
  Serial.print("Offset1: "); Serial.println(offset1);
  Serial.print("Offset2: "); Serial.println(offset2);
  Serial.print("Offset3: "); Serial.println(offset3);
  
  saveCalibrationToSPIFFS();
}

//
// clearSPIFFSValues() - Clears the calibration file from SPIFFS and resets values.
void clearSPIFFSValues() {
  if (SPIFFS.exists("/calibration.txt")) {
    SPIFFS.remove("/calibration.txt");
    Serial.println("SPIFFS calibration file cleared.");
  }
  calibrationFactor0 = 2280.0;
  calibrationFactor1 = 2280.0;
  calibrationFactor2 = 2280.0;
  calibrationFactor3 = 2280.0;
  offset0 = 0.0;
  offset1 = 0.0;
  offset2 = 0.0;
  offset3 = 0.0;
}

//
// getStableReading() - Takes 50 samples from the specified load cell after prompting the user.
// If variation exceeds 5 units, the measurement is retried.
float getStableReading(int cell, const char* phase) {
  while (true) {
    Serial.print("Press Enter to record ");
    Serial.print(phase);
    Serial.println(" measurement.");
    while (!Serial.available()) { delay(100); }
    String dummy = Serial.readStringUntil('\n');
    
    float sum = 0;
    float minVal = 1e9;
    float maxVal = -1e9;
    for (int i = 0; i < 50; i++) {
      float reading = 0;
      switch (cell) {
        case 0: reading = scale0.get_units(); break;
        case 1: reading = scale1.get_units(); break;
        case 2: reading = scale2.get_units(); break;
        case 3: reading = scale3.get_units(); break;
      }
      sum += reading;
      if (reading < minVal) minVal = reading;
      if (reading > maxVal) maxVal = reading;
      delay(15);
    }
    if ((maxVal - minVal) > 5) {
      Serial.println("Unstable readings detected (variation > 5 units). Please try again.");
      continue;
    }
    float average = sum / 50.0;
    Serial.print("Recorded average reading: ");
    Serial.println(average);
    return average;
  }
}

//
// getWeightInput() - Prompts the user to enter a weight (in kg) and returns the value.
float getWeightInput(String prompt) {
  Serial.println(prompt);
  while (true) {
    while (!Serial.available()) { delay(100); }
    String input = Serial.readStringUntil('\n');
    input.trim();
    float value = input.toFloat();
    if (value == 0 && input != "0" && input != "0.0") {
      Serial.println("Invalid input. Please enter a valid number.");
    } else {
      return value;
    }
  }
}

//
// saveCalibrationToSPIFFS() - Saves calibration factors and baseline offsets to SPIFFS.
// File format: cal0,cal1,cal2,cal3,offset0,offset1,offset2,offset3
void saveCalibrationToSPIFFS() {
  File file = SPIFFS.open("/calibration.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open calibration file for writing.");
    return;
  }
  file.printf("%f,%f,%f,%f,%f,%f,%f,%f\n", calibrationFactor0, calibrationFactor1, calibrationFactor2, calibrationFactor3, offset0, offset1, offset2, offset3);
  file.close();
}

//
// loadCalibrationFromSPIFFS() - Loads calibration factors and baseline offsets from SPIFFS if available.
void loadCalibrationFromSPIFFS() {
  if (!SPIFFS.exists("/calibration.txt")) {
    Serial.println("No calibration file found. Using default values.");
    return;
  }
  File file = SPIFFS.open("/calibration.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open calibration file for reading.");
    return;
  }
  String data = file.readStringUntil('\n');
  file.close();
  
  int idx1 = data.indexOf(',');
  int idx2 = data.indexOf(',', idx1 + 1);
  int idx3 = data.indexOf(',', idx2 + 1);
  int idx4 = data.indexOf(',', idx3 + 1);
  int idx5 = data.indexOf(',', idx4 + 1);
  int idx6 = data.indexOf(',', idx5 + 1);
  int idx7 = data.indexOf(',', idx6 + 1);
  
  if (idx1 == -1 || idx2 == -1 || idx3 == -1 || idx4 == -1 || idx5 == -1 || idx6 == -1 || idx7 == -1) {
    Serial.println("Calibration file format error. Using default values.");
    return;
  }
  
  calibrationFactor0 = data.substring(0, idx1).toFloat();
  calibrationFactor1 = data.substring(idx1 + 1, idx2).toFloat();
  calibrationFactor2 = data.substring(idx2 + 1, idx3).toFloat();
  calibrationFactor3 = data.substring(idx3 + 1, idx4).toFloat();
  offset0 = data.substring(idx4 + 1, idx5).toFloat();
  offset1 = data.substring(idx5 + 1, idx6).toFloat();
  offset2 = data.substring(idx6 + 1, idx7).toFloat();
  offset3 = data.substring(idx7 + 1).toFloat();
  
  Serial.println("Calibration factors and offsets loaded from SPIFFS.");
}

//
// filterSpike() - Filters out transient spikes by comparing the new value with the average of the last 5 samples.
// If the difference is more than 500 units, the new reading is replaced with the average.
float filterSpike(int cell, float newVal) {
  float avg = 0;
  int i;
  if (cell == 0) {
    if (sampleCount0 < NUM_SAMPLES) {
      lastReadings0[sampleIndex0] = newVal;
      sampleIndex0 = (sampleIndex0 + 1) % NUM_SAMPLES;
      sampleCount0++;
      return newVal;
    }
    for (i = 0; i < NUM_SAMPLES; i++) {
      avg += lastReadings0[i];
    }
    avg /= NUM_SAMPLES;
    if (abs(newVal - avg) > 500) {
      newVal = avg;
    }
    lastReadings0[sampleIndex0] = newVal;
    sampleIndex0 = (sampleIndex0 + 1) % NUM_SAMPLES;
    return newVal;
  } else if (cell == 1) {
    if (sampleCount1 < NUM_SAMPLES) {
      lastReadings1[sampleIndex1] = newVal;
      sampleIndex1 = (sampleIndex1 + 1) % NUM_SAMPLES;
      sampleCount1++;
      return newVal;
    }
    for (i = 0; i < NUM_SAMPLES; i++) {
      avg += lastReadings1[i];
    }
    avg /= NUM_SAMPLES;
    if (abs(newVal - avg) > 500) {
      newVal = avg;
    }
    lastReadings1[sampleIndex1] = newVal;
    sampleIndex1 = (sampleIndex1 + 1) % NUM_SAMPLES;
    return newVal;
  } else if (cell == 2) {
    if (sampleCount2 < NUM_SAMPLES) {
      lastReadings2[sampleIndex2] = newVal;
      sampleIndex2 = (sampleIndex2 + 1) % NUM_SAMPLES;
      sampleCount2++;
      return newVal;
    }
    for (i = 0; i < NUM_SAMPLES; i++) {
      avg += lastReadings2[i];
    }
    avg /= NUM_SAMPLES;
    if (abs(newVal - avg) > 500) {
      newVal = avg;
    }
    lastReadings2[sampleIndex2] = newVal;
    sampleIndex2 = (sampleIndex2 + 1) % NUM_SAMPLES;
    return newVal;
  } else if (cell == 3) {
    if (sampleCount3 < NUM_SAMPLES) {
      lastReadings3[sampleIndex3] = newVal;
      sampleIndex3 = (sampleIndex3 + 1) % NUM_SAMPLES;
      sampleCount3++;
      return newVal;
    }
    for (i = 0; i < NUM_SAMPLES; i++) {
      avg += lastReadings3[i];
    }
    avg /= NUM_SAMPLES;
    if (abs(newVal - avg) > 500) {
      newVal = avg;
    }
    lastReadings3[sampleIndex3] = newVal;
    sampleIndex3 = (sampleIndex3 + 1) % NUM_SAMPLES;
    return newVal;
  }
  return newVal;
}
