// mpu6050.h
// Leser MPU-6050 (kortet heter ofte GY-521) direkte over I2C, uten eget bibliotek.
//
// Kobling:  VCC -> 3V3    GND -> GND    SCL -> D1 (GPIO5)    SDA -> D2 (GPIO4)
// De andre pinnene på sensoren (XDA, XCL, AD0, INT) skal ikke kobles til.
//
// Etter mpuRead() ligger verdiene i mpu:
//   mpu.ax, mpu.ay, mpu.az   akselerasjon i g. Ligger boksen flatt og stille, er az omtrent 1.0
//   mpu.gx, mpu.gy, mpu.gz   rotasjon i grader per sekund. Stille boks gir omtrent 0
//   mpu.temp                 temperaturen inne i brikken (ikke romtemperatur)
//   mpu.ok                   true når sensoren svarer
//
// Kalibrering: ingen sensor er helt nøyaktig. En som ligger flatt kan vise Z = 1.35 i stedet for 1.00.
// mpuCalibrate() måler hvor mye sensoren bommer mens boksen ligger i ro, og lagrer avviket (offset)
// i minnet på ESP-en. Etterpå trekker mpuRead() avviket fra hver måling.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

const uint8_t MPU_ADDR = 0x68;        // 0x69 hvis AD0 er koblet til 3V3

struct MpuData {
  float ax, ay, az;
  float gx, gy, gz;
  float temp;
  bool ok;
};

struct MpuOffset {                    // avviket som trekkes fra hver måling
  float ax, ay, az;
  float gx, gy, gz;
};

struct MpuStored {                    // slik ligger avviket lagret i minnet (EEPROM)
  uint32_t magic;                     // fast tall som viser at det faktisk er lagret noe
  MpuOffset offset;
};

const uint32_t MPU_MAGIC = 0x4D505531;

MpuData mpu = { NAN, NAN, NAN, NAN, NAN, NAN, NAN, false };
MpuOffset mpuOffset = { 0, 0, 0, 0, 0, 0 };

// Skriver én verdi til ett register i sensoren. Returnerer true når sensoren svarer.
bool mpuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Vekker sensoren. Den starter i dvale når den får strøm.
bool mpuWake() {
  bool ok = mpuWrite(0x6B, 0x00);     // PWR_MGMT_1: ut av dvale
  mpuWrite(0x1A, 0x03);               // CONFIG: filter som demper støy (44 Hz)
  mpuWrite(0x1B, 0x00);               // GYRO_CONFIG: måleområde +/- 250 grader per sekund
  mpuWrite(0x1C, 0x00);               // ACCEL_CONFIG: måleområde +/- 2 g
  return ok;
}

// Lagrer avviket, så det overlever at strømmen tas.
void mpuSaveOffset() {
  MpuStored stored = { MPU_MAGIC, mpuOffset };
  EEPROM.put(0, stored);
  EEPROM.commit();
}

// Henter avviket fra forrige kalibrering. En boks som aldri er kalibrert får avvik 0.
void mpuLoadOffset() {
  MpuStored stored;
  EEPROM.get(0, stored);
  if (stored.magic == MPU_MAGIC) {
    mpuOffset = stored.offset;
  }
}

bool mpuBegin(int sda, int scl) {
  EEPROM.begin(64);
  mpuLoadOffset();
  Wire.begin(sda, scl);
  mpu.ok = mpuWake();
  return mpu.ok;
}

// Leser alle målingene. Ved feil blir verdiene NAN, og sensoren vekkes på nytt neste gang
// (f.eks. etter at en ledning har vært løs).
bool mpuRead() {
  if (!mpu.ok && !mpuWake()) {
    return false;
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                   // første register med måledata
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)MPU_ADDR, 14) != 14) {
    mpu = { NAN, NAN, NAN, NAN, NAN, NAN, NAN, false };
    return false;
  }

  int16_t raw[7];                     // aksX, aksY, aksZ, temp, gyroX, gyroY, gyroZ: to byte hver
  for (int i = 0; i < 7; i++) {
    uint8_t high = Wire.read();
    uint8_t low = Wire.read();
    raw[i] = (int16_t)((high << 8) | low);
  }

  mpu.ax = raw[0] / 16384.0 - mpuOffset.ax;    // 16384 trinn per g i måleområdet +/- 2 g
  mpu.ay = raw[1] / 16384.0 - mpuOffset.ay;
  mpu.az = raw[2] / 16384.0 - mpuOffset.az;
  mpu.temp = raw[3] / 340.0 + 36.53;
  mpu.gx = raw[4] / 131.0 - mpuOffset.gx;      // 131 trinn per grad/sekund i måleområdet +/- 250
  mpu.gy = raw[5] / 131.0 - mpuOffset.gy;
  mpu.gz = raw[6] / 131.0 - mpuOffset.gz;
  mpu.ok = true;
  return true;
}

// Fjerner kalibreringen. Da vises det sensoren faktisk måler.
void mpuClearOffset() {
  mpuOffset = { 0, 0, 0, 0, 0, 0 };
  mpuSaveOffset();
}

// Kalibrerer. Boksen må ligge flatt og helt i ro i ett sekund.
// Tar 50 målinger og bruker gjennomsnittet. Etterpå viser boksen X = 0, Y = 0 og Z = 1
// (Z = -1 hvis sensoren er montert opp ned), og gyroen viser 0.
// Returnerer false hvis sensoren ikke svarer, boksen ble flyttet, eller den ikke ligger flatt.
// Da beholdes den gamle kalibreringen.
bool mpuCalibrate() {
  const int COUNT = 50;
  const MpuOffset old = mpuOffset;
  mpuOffset = { 0, 0, 0, 0, 0, 0 };            // måler uten avvik mens vi kalibrerer

  float sum[6] = { 0, 0, 0, 0, 0, 0 };
  float lowest[3], highest[3];                 // for å oppdage at boksen ble flyttet
  for (int n = 0; n < COUNT; n++) {
    if (!mpuRead()) {
      mpuOffset = old;
      return false;
    }
    float v[6] = { mpu.ax, mpu.ay, mpu.az, mpu.gx, mpu.gy, mpu.gz };
    for (int i = 0; i < 6; i++) {
      sum[i] += v[i];
    }
    for (int i = 0; i < 3; i++) {
      if (n == 0 || v[i] < lowest[i]) lowest[i] = v[i];
      if (n == 0 || v[i] > highest[i]) highest[i] = v[i];
    }
    delay(20);
  }

  float avg[6];
  for (int i = 0; i < 6; i++) {
    avg[i] = sum[i] / COUNT;
  }
  bool moved = false;
  for (int i = 0; i < 3; i++) {
    if (highest[i] - lowest[i] > 0.10) moved = true;   // mer enn 0.1 g forskjell: ikke i ro
  }
  if (moved || fabs(avg[2]) < 0.5) {           // Z under 0.5 g: boksen ligger ikke flatt
    mpuOffset = old;
    return false;
  }

  float flat = avg[2] > 0 ? 1.0 : -1.0;        // det Z skal vise når boksen ligger slik
  mpuOffset = { avg[0], avg[1], avg[2] - flat, avg[3], avg[4], avg[5] };
  mpuSaveOffset();
  return true;
}
