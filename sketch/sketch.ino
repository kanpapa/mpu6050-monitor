/*
 * MPU6050 IMU on Arduino UNO Q  -- MCU side
 * I2C読み取り + ジャイロバイアス補正 + 相補フィルタ
 * 結果を Bridge.notify("imu_data", roll, pitch, yaw, temp) で 20Hz push
 */
#include <Arduino_RouterBridge.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

Adafruit_MPU6050 mpu;

static const uint8_t  MPU_ADDR   = 0x68;
static const float    ALPHA      = 0.98f;
static const float    RAD2DEG    = 57.2957795f;
static const uint32_t SAMPLE_US  = 10000; // 100Hz
static const uint32_t NOTIFY_MS  = 50;    //  20Hz

// ── エラーコード定義 ─────────────────────────────────────
enum ImuError {
  IMU_OK               = 0,
  IMU_ERR_BEGIN_FAILED = 1,  // mpu.begin()失敗(WHO_AM_I不一致など)
  IMU_ERR_READ_FAIL    = 2,  // 動作中にgetEvent()が続けて失敗
};

bool   sensorOk      = false;
int    imuErrorCode  = IMU_OK;
String imuErrorMsg   = "";

float roll = 0, pitch = 0, yaw = 0, tempC = 0;
float gxBias = 0, gyBias = 0, gzBias = 0;
uint32_t lastSampleUs = 0, lastNotifyMs = 0;
uint32_t lastInitAttemptMs = 0;
uint8_t  consecutiveReadFail = 0;

static const uint32_t REINIT_INTERVAL_MS   = 3000;  // 失敗中は3秒おきに再初期化を試みる
static const uint8_t  MAX_READ_FAIL_BEFORE_REINIT = 10;

// ── エラー状態をセットし、変化があればPythonへ通知 ────────
void setError(int code, const String &msg) {
  bool changed = (imuErrorCode != code);
  imuErrorCode = code;
  imuErrorMsg  = msg;
  if (changed) {
    Bridge.notify("imu_status", sensorOk, code, msg);
  }
}

// ── センサー初期化。失敗理由を区別して記録する ─────────────
bool initMPU() {
  if (!mpu.begin(MPU_ADDR, &Wire)) {
    setError(IMU_ERR_BEGIN_FAILED, "mpu.begin() failed (no ACK / WHO_AM_I mismatch)");
    return false;
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

  consecutiveReadFail = 0;
  setError(IMU_OK, "");
  return true;
}

bool calibrateGyro(int samples) {
  if (samples < 50)   samples = 50;
  if (samples > 2000) samples = 2000;
  double sx = 0, sy = 0, sz = 0;
  int got = 0;
  for (int i = 0; i < samples; i++) {
    sensors_event_t ta, tg, tt;
    if (mpu.getEvent(&ta, &tg, &tt)) {
      sx += tg.gyro.x; sy += tg.gyro.y; sz += tg.gyro.z;
      got++;
    }
    delay(2);
  }
  if (got == 0) return false;
  gxBias = (float)(sx / got) * RAD2DEG;
  gyBias = (float)(sy / got) * RAD2DEG;
  gzBias = (float)(sz / got) * RAD2DEG;
  return true;
}

// ── Bridge RPC ───────────────────────────────────────────
String rpc_calibrate(int samples) {
  if (!sensorOk) return String("{\"ok\":false,\"err\":\"sensor\"}");
  if (!calibrateGyro(samples)) return String("{\"ok\":false,\"err\":\"read\"}");
  return "{\"ok\":true,\"gx\":" + String(gxBias, 3) +
         ",\"gy\":" + String(gyBias, 3) +
         ",\"gz\":" + String(gzBias, 3) + "}";
}

String rpc_reset_yaw(int dummy) { (void)dummy; yaw = 0.0f; return String("{\"ok\":true}"); }

// センサー状態をいつでも問い合わせられるように拡張
String rpc_get_status(int dummy) {
  (void)dummy;
  String s = "{\"ok\":"; s += sensorOk ? "true" : "false";
  s += ",\"err_code\":" + String(imuErrorCode);
  s += ",\"err_msg\":\"" + imuErrorMsg + "\"";
  s += "}";
  return s;
}

uint8_t readWhoAmI() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  if (Wire.endTransmission(false) != 0) return 0xFF;  // 通信失敗マーカー
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

String rpc_read_raw(int dummy) {
  (void)dummy;
  sensors_event_t ta = {}, tg = {}, tt = {};
  bool ok = sensorOk && mpu.getEvent(&ta, &tg, &tt);
  String s = "{\"ok\":"; s += ok ? "true" : "false";
  s += ",\"err_code\":" + String(imuErrorCode);
  s += ",\"who\":" + String(readWhoAmI());
  s += ",\"ax\":" + String(ta.acceleration.x, 3);
  s += ",\"ay\":" + String(ta.acceleration.y, 3);
  s += ",\"az\":" + String(ta.acceleration.z, 3);
  s += ",\"gx\":" + String(tg.gyro.x * RAD2DEG, 3);
  s += ",\"gy\":" + String(tg.gyro.y * RAD2DEG, 3);
  s += ",\"gz\":" + String(tg.gyro.z * RAD2DEG, 3);
  s += ",\"temp\":" + String(tt.temperature, 2);
  s += "}";
  return s;
}

void setup() {
  Bridge.begin();
  Wire.begin();
  delay(100);

  // 起動直後は電源投入直後の過渡状態でACKが取れないことがあるため、数回リトライする
  const int MAX_INIT_RETRY = 5;
  for (int i = 0; i < MAX_INIT_RETRY && !sensorOk; i++) {
    sensorOk = initMPU();
    if (!sensorOk) delay(200);
  }
  if (sensorOk) calibrateGyro(500);

  Bridge.provide("imu_calibrate",  rpc_calibrate);
  Bridge.provide("imu_reset_yaw",  rpc_reset_yaw);
  Bridge.provide("imu_read_raw",   rpc_read_raw);
  Bridge.provide("imu_get_status", rpc_get_status);

  // 起動直後の状態を一度Pythonへ知らせる
  Bridge.notify("imu_status", sensorOk, imuErrorCode, imuErrorMsg);

  lastSampleUs = micros();
  lastNotifyMs = millis();
  lastInitAttemptMs = millis();
}

void loop() {
  if (!sensorOk) {
    // 未接続状態でも生きていることが分かるよう、遅い周期でimu_dataを送り続ける
    if (millis() - lastNotifyMs >= 1000) {
      lastNotifyMs = millis();
      Bridge.notify("imu_data", 0.0f, 0.0f, 0.0f, -273.0f);
    }
    // 数秒おきに自動で再初期化を試みる(配線を直した後に再起動不要にするため)
    if (millis() - lastInitAttemptMs >= REINIT_INTERVAL_MS) {
      lastInitAttemptMs = millis();
      if (initMPU()) {
        sensorOk = true;
        calibrateGyro(500);
      }
    }
    return;
  }

  uint32_t now = micros();
  if ((uint32_t)(now - lastSampleUs) >= SAMPLE_US) {
    float dt = (float)(now - lastSampleUs) / 1000000.0f;
    lastSampleUs = now;

    sensors_event_t ta, tg, tt;
    if (mpu.getEvent(&ta, &tg, &tt)) {
      consecutiveReadFail = 0;

      float ax = ta.acceleration.x, ay = ta.acceleration.y, az = ta.acceleration.z;
      float gx = tg.gyro.x * RAD2DEG - gxBias;
      float gy = tg.gyro.y * RAD2DEG - gyBias;
      float gz = tg.gyro.z * RAD2DEG - gzBias;
      tempC = tt.temperature;

      float rollAcc  = atan2f(ay, az) * RAD2DEG;
      float pitchAcc = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

      roll  = ALPHA * (roll  + gx * dt) + (1.0f - ALPHA) * rollAcc;
      pitch = ALPHA * (pitch + gy * dt) + (1.0f - ALPHA) * pitchAcc;
      yaw   = yaw + gz * dt;
      if (yaw >  180.0f) yaw -= 360.0f;
      if (yaw < -180.0f) yaw += 360.0f;
    } else {
      // 動作中に読み取りが続けて失敗する場合、センサーが外れた/バスが落ちたとみなす
      consecutiveReadFail++;
      if (consecutiveReadFail >= MAX_READ_FAIL_BEFORE_REINIT) {
        sensorOk = false;
        setError(IMU_ERR_READ_FAIL, "getEvent() failed repeatedly");
        lastInitAttemptMs = millis();
      }
    }
  }

  if (millis() - lastNotifyMs >= NOTIFY_MS) {
    lastNotifyMs = millis();
    Bridge.notify("imu_data", roll, pitch, yaw, tempC);
  }
}