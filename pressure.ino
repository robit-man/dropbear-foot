/*
  Four-HX711 Load-Cell Reader @ 80 Hz
  ESP32 DevKit v1 · SCK=GPIO4 · DOUT=32,12,13,15
  CSV stream: weight0,weight1,weight2,weight3
  Commands: /calibrate /drift /clear   (persist in SPIFFS)
*/

#include "HX711.h"
#include "SPIFFS.h"

#define SHARED_HX711_SCK 4
#define LOADCELL_DOUT_PIN0 32
#define LOADCELL_DOUT_PIN1 12
#define LOADCELL_DOUT_PIN2 13
#define LOADCELL_DOUT_PIN3 15

HX711 scale0, scale1, scale2, scale3;

float calibrationFactor0 = 2280.0, calibrationFactor1 = 2280.0,
      calibrationFactor2 = 2280.0, calibrationFactor3 = 2280.0;
float offset0 = 0.0, offset1 = 0.0, offset2 = 0.0, offset3 = 0.0;

#define PRESS_THRESHOLD   0.5
#define NUM_SAMPLES       3
#define SAMPLE_INTERVAL_US 12500
#define SPIKE_THRESHOLD   500

float lastReadings0[NUM_SAMPLES] = {0}, lastReadings1[NUM_SAMPLES] = {0},
      lastReadings2[NUM_SAMPLES] = {0}, lastReadings3[NUM_SAMPLES] = {0};
int sampleIndex0 = 0, sampleCount0 = 0,
    sampleIndex1 = 0, sampleCount1 = 0,
    sampleIndex2 = 0, sampleCount2 = 0,
    sampleIndex3 = 0, sampleCount3 = 0;

void calibrate();
void resetDrift();
void clearSPIFFSValues();
float getStableReading(int cell,const char* phase);
float getWeightInput(String prompt);
void saveCalibrationToSPIFFS();
void loadCalibrationFromSPIFFS();
float filterSpike(int cell,float v);
bool allReady();
void burstReadRaw(long raw[4]);
float rawToUnits(long raw,HX711& h,float drift);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed");

  loadCalibrationFromSPIFFS();

  scale0.begin(LOADCELL_DOUT_PIN0, SHARED_HX711_SCK);
  scale1.begin(LOADCELL_DOUT_PIN1, SHARED_HX711_SCK);
  scale2.begin(LOADCELL_DOUT_PIN2, SHARED_HX711_SCK);
  scale3.begin(LOADCELL_DOUT_PIN3, SHARED_HX711_SCK);

  scale0.set_gain(128); scale1.set_gain(128);
  scale2.set_gain(128); scale3.set_gain(128);

  scale0.tare(); scale1.tare(); scale2.tare(); scale3.tare();

  scale0.set_scale(calibrationFactor0);
  scale1.set_scale(calibrationFactor1);
  scale2.set_scale(calibrationFactor2);
  scale3.set_scale(calibrationFactor3);

  pinMode(SHARED_HX711_SCK, OUTPUT); digitalWrite(SHARED_HX711_SCK, LOW);

  Serial.println("Scale0,Scale1,Scale2,Scale3");
  Serial.println("Commands: /calibrate  /drift  /clear");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim();
    if      (cmd.equalsIgnoreCase("/calibrate")) calibrate();
    else if (cmd.equalsIgnoreCase("/drift"))     resetDrift();
    else if (cmd.equalsIgnoreCase("/clear"))     clearSPIFFSValues();
  }

  static unsigned long lastUs = 0;
  unsigned long now = micros();
  if (now - lastUs < SAMPLE_INTERVAL_US) return;
  lastUs = now;

  if (!allReady()) return;

  long raw[4]; burstReadRaw(raw);

  float w0 = filterSpike(0, rawToUnits(raw[0], scale0, offset0));
  float w1 = filterSpike(1, rawToUnits(raw[1], scale1, offset1));
  float w2 = filterSpike(2, rawToUnits(raw[2], scale2, offset2));
  float w3 = filterSpike(3, rawToUnits(raw[3], scale3, offset3));

  Serial.printf("%.2f,%.2f,%.2f,%.2f\n", w0, w1, w2, w3);
}

bool allReady() {
  return digitalRead(LOADCELL_DOUT_PIN0)==LOW &&
         digitalRead(LOADCELL_DOUT_PIN1)==LOW &&
         digitalRead(LOADCELL_DOUT_PIN2)==LOW &&
         digitalRead(LOADCELL_DOUT_PIN3)==LOW;
}

void burstReadRaw(long raw[4]) {
  raw[0]=raw[1]=raw[2]=raw[3]=0;
  for (int i=0;i<24;i++) {
    digitalWrite(SHARED_HX711_SCK, HIGH);
    raw[0]=(raw[0]<<1)|digitalRead(LOADCELL_DOUT_PIN0);
    raw[1]=(raw[1]<<1)|digitalRead(LOADCELL_DOUT_PIN1);
    raw[2]=(raw[2]<<1)|digitalRead(LOADCELL_DOUT_PIN2);
    raw[3]=(raw[3]<<1)|digitalRead(LOADCELL_DOUT_PIN3);
    digitalWrite(SHARED_HX711_SCK, LOW);
  }
  digitalWrite(SHARED_HX711_SCK, HIGH); digitalWrite(SHARED_HX711_SCK, LOW);
  for (int i=0;i<4;i++) if (raw[i]&0x800000) raw[i]|=0xFF000000;
}

float rawToUnits(long raw,HX711& h,float drift) {
  return ((raw - h.get_offset()) / h.get_scale()) - drift;
}

void calibrate() {
  Serial.println("Calibration mode: press on one cell >0.5 units");
  int cell=-1;
  while(cell<0){
    if(allReady()){
      long raw[4]; burstReadRaw(raw);
      float w0=rawToUnits(raw[0],scale0,offset0);
      float w1=rawToUnits(raw[1],scale1,offset1);
      float w2=rawToUnits(raw[2],scale2,offset2);
      float w3=rawToUnits(raw[3],scale3,offset3);
      if(w0>PRESS_THRESHOLD)cell=0;
      else if(w1>PRESS_THRESHOLD)cell=1;
      else if(w2>PRESS_THRESHOLD)cell=2;
      else if(w3>PRESS_THRESHOLD)cell=3;
    }
    delay(50);
  }
  Serial.printf("Cell %d selected. Confirm Y/N\n",cell);
  while(!Serial.available()) delay(50);
  String c=Serial.readStringUntil('\n'); c.trim();
  if(!(c.equalsIgnoreCase("Y")||c.equalsIgnoreCase("YES"))){Serial.println("Abort");return;}

  Serial.println("Remove load, press Enter"); while(!Serial.available())delay(50); Serial.readStringUntil('\n');
  float baseline=getStableReading(cell,"baseline");

  Serial.println("Place weight #1, press Enter"); while(!Serial.available())delay(50); Serial.readStringUntil('\n');
  float r1=getStableReading(cell,"first"); float w1=getWeightInput("Weight kg:");

  Serial.println("Place weight #2, press Enter"); while(!Serial.available())delay(50); Serial.readStringUntil('\n');
  float r2=getStableReading(cell,"second"); float w2=getWeightInput("Weight kg:");

  float newCF=((r1-baseline)/w1+(r2-baseline)/w2)/2.0;
  if(cell==0){calibrationFactor0=newCF; scale0.set_scale(newCF);}
  else if(cell==1){calibrationFactor1=newCF; scale1.set_scale(newCF);}
  else if(cell==2){calibrationFactor2=newCF; scale2.set_scale(newCF);}
  else {calibrationFactor3=newCF; scale3.set_scale(newCF);}
  saveCalibrationToSPIFFS();
  Serial.println("Calibration saved");
}

void resetDrift() {
  Serial.println("Drift reset: ensure no load, press Enter");
  while(!Serial.available())delay(50); Serial.readStringUntil('\n');
  float sum[4]={0,0,0,0},mn[4]={1e9,1e9,1e9,1e9},mx[4]={-1e9,-1e9,-1e9,-1e9};
  for(int k=0;k<50;k++){
    while(!allReady()){}
    long r[4]; burstReadRaw(r);
    float w[4]={rawToUnits(r[0],scale0,0),rawToUnits(r[1],scale1,0),
                rawToUnits(r[2],scale2,0),rawToUnits(r[3],scale3,0)};
    for(int i=0;i<4;i++){sum[i]+=w[i]; if(w[i]<mn[i])mn[i]=w[i]; if(w[i]>mx[i])mx[i]=w[i];}
    delay(15);
  }
  for(int i=0;i<4;i++) if(mx[i]-mn[i]>5){Serial.println("Unstable");return;}
  offset0=sum[0]/50.0; offset1=sum[1]/50.0; offset2=sum[2]/50.0; offset3=sum[3]/50.0;
  saveCalibrationToSPIFFS();
  Serial.println("Offsets saved");
}

void clearSPIFFSValues() {
  if(SPIFFS.exists("/calibration.txt")) SPIFFS.remove("/calibration.txt");
  calibrationFactor0=calibrationFactor1=calibrationFactor2=calibrationFactor3=2280.0;
  offset0=offset1=offset2=offset3=0.0;
  scale0.set_scale(calibrationFactor0); scale1.set_scale(calibrationFactor1);
  scale2.set_scale(calibrationFactor2); scale3.set_scale(calibrationFactor3);
}

float getStableReading(int cell,const char* phase){
  while(true){
    float sum=0,mn=1e9,mx=-1e9;
    for(int i=0;i<50;i++){
      while(!allReady()){}
      long r[4]; burstReadRaw(r);
      float v=(cell==0)?rawToUnits(r[0],scale0,offset0):
               (cell==1)?rawToUnits(r[1],scale1,offset1):
               (cell==2)?rawToUnits(r[2],scale2,offset2):
                          rawToUnits(r[3],scale3,offset3);
      sum+=v; if(v<mn)mn=v; if(v>mx)mx=v; delay(15);
    }
    if(mx-mn>5) continue;
    return sum/50.0;
  }
}

float getWeightInput(String prompt){
  Serial.println(prompt);
  while(true){
    while(!Serial.available())delay(50);
    String s=Serial.readStringUntil('\n'); s.trim();
    float v=s.toFloat();
    if(v==0&&(s!="0"&&s!="0.0")) Serial.println("Invalid");
    else return v;
  }
}

void saveCalibrationToSPIFFS(){
  File f=SPIFFS.open("/calibration.txt",FILE_WRITE);
  if(!f) return;
  f.printf("%f,%f,%f,%f,%f,%f,%f,%f\n",
           calibrationFactor0,calibrationFactor1,calibrationFactor2,calibrationFactor3,
           offset0,offset1,offset2,offset3);
  f.close();
}

void loadCalibrationFromSPIFFS(){
  if(!SPIFFS.exists("/calibration.txt")) return;
  File f=SPIFFS.open("/calibration.txt",FILE_READ);
  if(!f) return;
  String l=f.readStringUntil('\n'); f.close();
  int i[7],p=-1; for(int k=0;k<7;k++){i[k]=l.indexOf(',',p+1); p=i[k];}
  if(i[6]==-1) return;
  calibrationFactor0=l.substring(0,i[0]).toFloat();
  calibrationFactor1=l.substring(i[0]+1,i[1]).toFloat();
  calibrationFactor2=l.substring(i[1]+1,i[2]).toFloat();
  calibrationFactor3=l.substring(i[2]+1,i[3]).toFloat();
  offset0=l.substring(i[3]+1,i[4]).toFloat();
  offset1=l.substring(i[4]+1,i[5]).toFloat();
  offset2=l.substring(i[5]+1,i[6]).toFloat();
  offset3=l.substring(i[6]+1).toFloat();
}

float filterSpike(int cell,float v){
  float* b; int* idx; int* cnt;
  if(cell==0){b=lastReadings0;idx=&sampleIndex0;cnt=&sampleCount0;}
  else if(cell==1){b=lastReadings1;idx=&sampleIndex1;cnt=&sampleCount1;}
  else if(cell==2){b=lastReadings2;idx=&sampleIndex2;cnt=&sampleCount2;}
  else {b=lastReadings3;idx=&sampleIndex3;cnt=&sampleCount3;}
  if(*cnt<NUM_SAMPLES){b[*idx]=v;*idx=(*idx+1)%NUM_SAMPLES;(*cnt)++;return v;}
  float avg=0; for(int i=0;i<NUM_SAMPLES;i++) avg+=b[i]; avg/=NUM_SAMPLES;
  if(fabs(v-avg)>SPIKE_THRESHOLD) v=avg;
  b[*idx]=v; *idx=(*idx+1)%NUM_SAMPLES; return v;
}
