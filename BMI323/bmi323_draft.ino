#include <Wire.h>

#define INC_ADDRESS 0x68
#define ACC_CONF  0x20  //Page 91
#define GYR_CONF  0x21  //Page 93
#define CMD       0x7E  //Page 65

uint16_t x, y, z;
float accel_g_x, accel_g_y, accel_g_z;
uint16_t gyr_x, gyr_y, gyr_z;
float gyr_g_x, gyr_g_y, gyr_g_z;

//conversion in g: ±2g: 16.384, ±4g: 8.192, ±8g: 4.096, ±16g: 2.048
uint16_t sensitiv_acc = 16.384;
//uint16_t sensitiv_gyr = 131.072;
uint16_t sensitiv_gyr = 16.384;

uint16_t  temperature = 0;
float     temperatureInDegree = 0.f;

void setup(void) {  
  Serial.begin(115200); 
  //Accelerometer
  Wire.begin();  
  Wire.setClock(400000);      // I2C Fast Mode (400kHz)  
  softReset();  
  /*
   * Acc_Conf P.91
   * mode:        0x7000  -> High
   * average:     0x0000  -> No
   * filtering:   0x0000  -> ODR/4
   * range:       0x0000  -> 2G
   * ODR:         0x000B  -> 800Hz
   * Total:       0x700B
   */
  writeRegister16(ACC_CONF,0x708B);//Setting accelerometer  
  /*
   * Gyr_Conf P.93
   * mode:        0x7000  -> High
   * average:     0x0000  -> No
   * filtering:   0x0080  -> ODR/4
   * range:       0x0000  -> 125kdps
   * ODR:         0x000B  -> 800Hz
   * Total:       0x708B
   */
   /*ILANA CONF
   * Gyr_Conf P.93
   * mode:        0x4000  -> Normal
   * average:     0x0000  -> No
   * filtering:   0x0000  -> ODR/2  
   * range:       0x0040  -> 2000dps
   * ODR:         0x0008  -> 100Hz
   * Total:       0x4048
   */
    /*ILANA CONF2
   * Gyr_Conf P.93
   * mode:        0x4000  -> Normal
   * average:     0x0000  -> No
   * filtering:   0x0080  -> ODR/4  
   * range:       0x0010  -> 250dps
   * ODR:         0x0006  -> 25Hz
   * Total:       0x4098
   */
  writeRegister16(GYR_CONF,0x4048);//Setting gyroscope    
}

void softReset(){  
  writeRegister16(CMD, 0xDEAF);
  delay(50);    
}

void loop() {

  // if(readRegister16(0x02) == 0x00) {
    //Read ChipID
    // Serial.print("ChipID:");
    // Serial.print(readRegister16(0x00));    
    readAllAccel();             // read all accelerometer/gyroscope/temperature data     
    //Serial.print(" \tx:");
    //Serial.print(x);
    //Serial.print(" \ty:");
    //Serial.print(y);
    // Serial.print(" \tz:");
    // Serial.print(z);
    Serial.print(" \taccel_g_x:"); Serial.print(accel_g_x, 3);
    Serial.print(" \taccel_g_y:"); Serial.print(accel_g_y, 3);
    Serial.print(" \taccel_g_z:"); Serial.print(accel_g_z, 3);
    // Serial.print(" \tgyr_x:");
    // Serial.print(gyr_x);
    // Serial.print(" \tgyr_y:");
    // Serial.print(gyr_y);
    // Serial.print(" \tgyr_z:");
    // Serial.print(gyr_z);
    Serial.print(" \tgyr_g_x:"); Serial.print(gyr_g_x, 3);
    Serial.print(" \tgyr_g_y:"); Serial.print(gyr_g_y, 3);
    Serial.print(" \tgyr_g_z:"); Serial.print(gyr_g_z, 3);
    Serial.print(" \ttemp:");
    Serial.println(temperatureInDegree);    
  //}
  delay(100); 
}

//Write data in 16 bits
void writeRegister16(uint16_t reg, uint16_t value) {
  Wire.beginTransmission(INC_ADDRESS);
  Wire.write(reg);
  //Low 
  Wire.write((uint16_t)value & 0xff);
  //High
  Wire.write((uint16_t)value >> 8);
  Wire.endTransmission();
}

//Read data in 16 bits
uint16_t readRegister16(uint8_t reg) {
  Wire.beginTransmission(INC_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);
  int n = Wire.requestFrom(INC_ADDRESS, 4);  
  uint16_t data[20];
  int i =0;
  while(Wire.available()){
    data[i] = Wire.read();
    i++;
  }  
  return (data[3]   | data[2] << 8);
}

//Read all axis
void readAllAccel() {
  Wire.beginTransmission(INC_ADDRESS);
  Wire.write(0x03);
  Wire.endTransmission();
  Wire.requestFrom(INC_ADDRESS, 20);
  uint16_t data[20];
  int i =0;
  while(Wire.available()){
    data[i] = Wire.read();
    i++;
  }

  //Offset = 2 because the 2 first bytes are dummy (useless)
  int offset = 2;  
  x =             (data[offset + 0]   | (uint16_t)data[offset + 1] << 8);  //0x03
  y =             (data[offset + 2]   | (uint16_t)data[offset + 3] << 8);  //0x04
  z =             (data[offset + 4]   | (uint16_t)data[offset + 5] << 8);  //0x05
  gyr_x =         (data[offset + 6]   | (uint16_t)data[offset + 7] << 8);  //0x06
  gyr_y =         (data[offset + 8]   | (uint16_t)data[offset + 9] << 8);  //0x07
  gyr_z =         (data[offset + 10]  | (uint16_t)data[offset + 11] << 8); //0x08
  temperature =   (data[offset + 12]  | (uint16_t)data[offset + 13] << 8); //0x09
  temperatureInDegree = (temperature/512.f) + 23.0f;  

  accel_g_x = (int16_t)x / sensitiv_acc;
  accel_g_y = (int16_t)y / sensitiv_acc;
  accel_g_z = (int16_t)z / sensitiv_acc;

  gyr_g_x = (int16_t)gyr_x / sensitiv_gyr;
  gyr_g_y = (int16_t)gyr_y / sensitiv_gyr;
  gyr_g_z = (int16_t)gyr_z / sensitiv_gyr;
  
}
