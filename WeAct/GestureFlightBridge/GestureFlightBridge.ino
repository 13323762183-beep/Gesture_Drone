#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>
#include "flight_config.h"
#include "command_protocol.h"

#if !ARDUINO_USB_CDC_ON_BOOT
#error Enable USB CDC On Boot
#endif

HardwareSerial ax7020(1);
WiFiUDP udp;
const IPAddress drone(DRONE_IP_A, DRONE_IP_B, DRONE_IP_C, DRONE_IP_D);

enum FlightState { WAIT_LINK, GROUNDED, ARMING, TAKEOFF, FLYING, LANDING, FAULT };
FlightState state = WAIT_LINK;
uint8_t txSeq = 0, droneSysId = 1;
bool haveHeartbeat = false, armed = false, haveCommand = false;
bool udpBound = false;
bool sessionEnabled = true, unlockReleased = false;
bool stopRequested = false;
uint32_t heartbeatAt = 0, commandAt = 0, stateAt = 0, wifiRetryAt = 0;
uint32_t lastCommandSeq = 0;
char activeAction[12] = "STOP";

static const char *stateName() {
  static const char *names[] = {"WAIT_LINK","GROUNDED","ARMING","TAKEOFF","FLYING","LANDING","FAULT"};
  return names[state];
}

static bool flightValuesReady() {
  return isfinite(FLIGHT_TAKEOFF_THROTTLE) && isfinite(FLIGHT_HOVER_THROTTLE) &&
         isfinite(FLIGHT_LAND_THROTTLE) &&
         FLIGHT_TAKEOFF_THROTTLE >= 0 && FLIGHT_TAKEOFF_THROTTLE <= 1 &&
         FLIGHT_HOVER_THROTTLE >= 0 && FLIGHT_HOVER_THROTTLE <= 1 &&
         FLIGHT_LAND_THROTTLE >= 0 && FLIGHT_LAND_THROTTLE <= 1;
}

static uint16_t mavCrcAdd(uint16_t crc, uint8_t value) {
  uint8_t t = value ^ (crc & 0xff); t ^= t << 4;
  return (crc >> 8) ^ ((uint16_t)t << 8) ^ ((uint16_t)t << 3) ^ (t >> 4);
}

static bool sendMavlink(uint32_t id, uint8_t extra, const uint8_t *payload, uint8_t len) {
  if (WiFi.status() != WL_CONNECTED) return false;
  uint8_t frame[280] = {0xfd, len, 0, 0, txSeq++, 255, 190,
                        (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16)};
  memcpy(frame + 10, payload, len);
  uint16_t crc = 0xffff;
  for (int i = 1; i < 10 + len; ++i) crc = mavCrcAdd(crc, frame[i]);
  crc = mavCrcAdd(crc, extra);
  frame[10 + len] = crc; frame[11 + len] = crc >> 8;
  udp.beginPacket(drone, DRONE_UDP_PORT); udp.write(frame, 12 + len);
  return udp.endPacket() == 1;
}

static bool armDrone(bool arm) {
  if (!FLIGHT_OUTPUT_ENABLED || (arm && !sessionEnabled)) return false;
  uint8_t p[33] = {0}; float value = arm ? 1.0f : 0.0f;
  memcpy(p, &value, 4); p[28] = 400 & 0xff; p[29] = 400 >> 8;
  p[30] = droneSysId; p[31] = 1;
  return sendMavlink(76, 152, p, 33);
}

static bool manualControl(float roll, float pitch, float throttle) {
  if (!FLIGHT_OUTPUT_ENABLED || !sessionEnabled) return false;
  static float currentThrottle = 0.0f;
  static float currentRoll = 0.0f, currentPitch = 0.0f;
  static uint32_t updatedAt = 0;
  uint32_t now = millis();
  float dt = min((uint32_t)(now - updatedAt), (uint32_t)100) / 1000.0f;
  float step = FLIGHT_THROTTLE_RATE * dt;
  updatedAt = now;
  if (state == GROUNDED || state == ARMING || !armed) currentThrottle = 0.0f;
  else currentThrottle += constrain(throttle - currentThrottle, -step, step);
  throttle = currentThrottle;
  float directionStep = FLIGHT_DIRECTION_RATE * dt;
  if (state == GROUNDED || state == ARMING || !armed) currentRoll = currentPitch = 0.0f;
  else {
    currentRoll += constrain(roll-currentRoll, -directionStep, directionStep);
    currentPitch += constrain(pitch-currentPitch, -directionStep, directionStep);
  }
  roll = currentRoll; pitch = currentPitch;
  int16_t x = (int16_t)constrain((int)(pitch * 1000), -1000, 1000);
  int16_t y = (int16_t)constrain((int)(roll * 1000), -1000, 1000);
  int16_t z = (int16_t)constrain((int)(throttle * 1000), 0, 1000);
  int16_t r = 0; uint16_t buttons = 0;
  uint8_t p[11] = {0};
  memcpy(p, &x, 2); memcpy(p + 2, &y, 2); memcpy(p + 4, &z, 2);
  memcpy(p + 6, &r, 2); memcpy(p + 8, &buttons, 2); p[10] = droneSysId;
  return sendMavlink(69, 243, p, 11);
}

static bool heartbeatFresh() { return haveHeartbeat && millis() - heartbeatAt < FLIGHT_HEARTBEAT_MS; }

static void startLanding(const char *reason) {
  if (!armed) { state = GROUNDED; manualControl(0, 0, 0); return; }
  if (!flightValuesReady()) { state = FAULT; Serial.printf("[FAULT] cannot land: values unset (%s)\n", reason); return; }
  if (state != LANDING) {
    Serial.printf("[LAND] %s\n", reason);
    state = LANDING; stateAt = millis(); strlcpy(activeAction, "LAND", sizeof(activeAction));
  }
}

static void applyFlyingAction(const char *action) {
  float roll = 0, pitch = 0;
  if (!strcmp(action, "FORWARD")) pitch = FLIGHT_MOVE_PITCH;
  else if (!strcmp(action, "BACK")) pitch = -FLIGHT_MOVE_PITCH;
  else if (!strcmp(action, "LEFT")) roll = -FLIGHT_MOVE_ROLL;
  else if (!strcmp(action, "RIGHT")) roll = FLIGHT_MOVE_ROLL;
  manualControl(roll, pitch, !strcmp(action,"UNLOCK") ? FLIGHT_TAKEOFF_THROTTLE : FLIGHT_HOVER_THROTTLE);
}

static void handleCommand(const CommandPacket &p) {
  if (haveCommand && (int32_t)(p.seq - lastCommandSeq) <= 0) return;
  haveCommand = true; lastCommandSeq = p.seq; commandAt = millis();
  if (strcmp(activeAction, p.action)) Serial.printf("[ACTION] %s seq=%lu\n", p.action, (unsigned long)p.seq);
  strlcpy(activeAction, p.action, sizeof(activeAction));
  if (strcmp(p.action, "UNLOCK")) unlockReleased = true;
  if (!strcmp(p.action, "LAND")) {
    startLanding("FIST");
    return;
  }
  if (!sessionEnabled) {
    if (!strcmp(p.action,"UNLOCK")) {
      static uint32_t noticeAt=0;
      if (millis()-noticeAt>1000) { Serial.println("[REFUSED] session locked; send ENABLE"); noticeAt=millis(); }
    }
    return;
  }

  if (!strcmp(p.action, "CALIBRATING")) return;
  if (!strcmp(p.action, "UNLOCK")) {
    if (state != GROUNDED || armed || !unlockReleased) {
      static uint32_t noticeAt=0;
      if (millis()-noticeAt>1000) {
        Serial.printf("[ARM_GATE] state=%s armed=%d released=%d hb=%d session=%d\n",stateName(),armed,unlockReleased,heartbeatFresh(),sessionEnabled);
        noticeAt=millis();
      }
      return;
    }
    if (!flightValuesReady()) { Serial.println("[REFUSED] fill flight_config.h first"); return; }
    if (!heartbeatFresh()) { Serial.println("[REFUSED] no fresh drone heartbeat"); return; }
    manualControl(0,0,0);
    if (armDrone(true)) { unlockReleased=false; state = ARMING; stateAt = millis(); Serial.println("[ARM] requested"); }
    else Serial.println("[REFUSED] ARM packet send failed");
    return;
  }
  if (state == FLYING && (!strcmp(p.action, "HOLD") || !strcmp(p.action, "STOP") ||
      !strcmp(p.action, "FORWARD") || !strcmp(p.action, "BACK") ||
      !strcmp(p.action, "LEFT") || !strcmp(p.action, "RIGHT"))) applyFlyingAction(p.action);
}

static void readAx7020() {
  static char line[128]; static size_t used = 0; static bool overflow = false;
  for (int budget = 0; budget < 96 && ax7020.available(); ++budget) {
    char c = (char)ax7020.read(); if (c == '\r') continue;
    if (c == '\n') {
      line[used] = 0;
      if (used && !overflow) {
        if (!strcmp(line, "GC_HELLO")) {
          if (sessionEnabled && armed) startLanding("AX7020 reboot");
          haveCommand = false; unlockReleased = false;
        } else { CommandPacket p = {}; if (commandParse(line, p)) handleCommand(p); }
      }
      used = 0; overflow = false;
    } else if (!overflow) {
      if (used < sizeof(line) - 1) line[used++] = c; else overflow = true;
    }
  }
}

static void readTelemetry() {
  int n = udp.parsePacket(); if (n <= 0) return;
  uint8_t b[1500]; IPAddress source = udp.remoteIP(); int port = udp.remotePort();
  n = udp.read(b, sizeof(b)); if (source != drone || port != DRONE_UDP_PORT) return;
  for (int i = 0; i < n;) {
    if (b[i] != 0xfd && b[i] != 0xfe) { ++i; continue; }
    bool v2 = b[i] == 0xfd; int h = v2 ? 10 : 6;
    if (i + h > n) break;
    int len = b[i + 1], total = h + len + 2 + ((v2 && (b[i + 2] & 1)) ? 13 : 0);
    if (i + total > n) break;
    uint32_t id = v2 ? b[i+7] | ((uint32_t)b[i+8] << 8) | ((uint32_t)b[i+9] << 16) : b[i+5];
    uint8_t extra = id == 0 ? 50 : 0;
    if (!extra) { i += total; continue; }
    uint16_t crc = 0xffff;
    for (int k = i + 1; k < i + h + len; ++k) crc = mavCrcAdd(crc, b[k]);
    crc = mavCrcAdd(crc, extra);
    if (crc != (uint16_t)(b[i+h+len] | ((uint16_t)b[i+h+len+1] << 8))) { i += total; continue; }
    if (id == 0 && len >= 9) {
      uint8_t *p = b + i + h; droneSysId = b[i + (v2 ? 5 : 3)];
      haveHeartbeat = true; heartbeatAt = millis(); armed = (p[6] & 128) != 0;
      if (!armed && (state == WAIT_LINK || state == FAULT)) state = GROUNDED;
    }
    i += total;
  }
}

static void tickFlight() {
  if (stopRequested) {
    static uint32_t lastStopAt = 0;
    if (millis() - lastStopAt >= 50) { armDrone(false); lastStopAt = millis(); }
    return;
  }
  if (!sessionEnabled || !FLIGHT_OUTPUT_ENABLED) return;
  uint32_t now = millis();
  if (armed && (!heartbeatFresh() || !haveCommand || now - commandAt > FLIGHT_FRAME_TIMEOUT_MS)) {
    armDrone(false); unlockReleased = false; state = FAULT;
    return;
  }

  if (state == ARMING) {
    manualControl(0, 0, 0);
    if (armed) { state = TAKEOFF; stateAt = now; Serial.println("[TAKEOFF]"); }
    else if (now - stateAt > 2000) { state = FAULT; Serial.println("[FAULT] arm timeout"); }
  } else if (state == TAKEOFF) {
    manualControl(0, 0, FLIGHT_TAKEOFF_THROTTLE);
    if (strcmp(activeAction, "UNLOCK")) state = FLYING;
  } else if (state == FLYING) {
    applyFlyingAction(activeAction);
  } else if (state == LANDING) {
    manualControl(0, 0, FLIGHT_LAND_THROTTLE);
    if (now - stateAt >= FLIGHT_LAND_MS) {
      armDrone(false); sessionEnabled = false; unlockReleased = false; state = WAIT_LINK;
      Serial.println("[LAND] timed disarm requested; ENABLE required to restart");
    }
    if (!armed) { state = GROUNDED; strlcpy(activeAction, "STOP", sizeof(activeAction)); Serial.println("[LANDED]"); }
  } else if (state == GROUNDED) {
    manualControl(0, 0, 0);
  }
}

static void usbConsole() {
  static char line[24]; static size_t used = 0;
  while (Serial.available()) {
    char c = (char)Serial.read(); if (c == '\r') continue;
    if (c == '\n') {
      line[used] = 0;
      if (!strcmp(line, "STATUS")) { Serial.printf("session=%d stop=%d released=%d output=%d\n",sessionEnabled,stopRequested,unlockReleased,FLIGHT_OUTPUT_ENABLED); Serial.printf("state=%s wifi=%d hb=%d armed=%d action=%s ax_age=%lu configured=%d\n",
        stateName(), WiFi.status() == WL_CONNECTED, heartbeatFresh(), armed, activeAction,
        (unsigned long)(millis() - commandAt), flightValuesReady()); }
      else if (!strcmp(line, "ENABLE")) {
        if (heartbeatFresh() && !armed && haveCommand && millis()-commandAt < FLIGHT_FRAME_TIMEOUT_MS) {
          sessionEnabled = true; unlockReleased = false; stopRequested = false;
          Serial.printf("[SESSION] enabled output=%d; release V before triggering\n", FLIGHT_OUTPUT_ENABLED);
        } else Serial.println("[REFUSED] need fresh AX7020 and disarmed drone heartbeat");
      }
      else if (!strcmp(line, "RELEASE")) {
        sessionEnabled = false; unlockReleased = false; state = WAIT_LINK;
        Serial.println("[RELEASED] all control transmission stopped; use drone's manual controller");
      }
      else if (!strcmp(line, "STOP")) {
        stopRequested = true;
        armDrone(false); sessionEnabled = false; unlockReleased = false; state = WAIT_LINK;
        Serial.println("[STOP] disarm requested; ENABLE required to restart");
      }
      else if (!strcmp(line, "LAND")) startLanding("USB");
      used = 0;
    } else if (used < sizeof(line) - 1) line[used++] = c;
  }
}

void setup() {
  Serial.begin(115200);
  ax7020.begin(115200, SERIAL_8N1, AX7020_UART_RX_PIN, AX7020_UART_TX_PIN);
  if (!commandProtocolSelfTest()) { state = FAULT; Serial.println("[SELFTEST] FAIL"); return; }
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(DRONE_WIFI_SSID, DRONE_WIFI_PASSWORD);
  wifiRetryAt = millis();
  Serial.printf("[READY] protocol=PASS configured=%d\n", flightValuesReady());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !udpBound) {
    udp.stop(); udp.begin(DRONE_UDP_PORT); udpBound = true;
    haveHeartbeat = false;
    Serial.printf("[WIFI] %s\n", WiFi.localIP().toString().c_str());
  } else if (WiFi.status() != WL_CONNECTED && udpBound) {
    udp.stop(); udpBound = false; haveHeartbeat = false;
    sessionEnabled = false; unlockReleased = false; haveCommand = false; state = WAIT_LINK;
  }
  if (WiFi.status() != WL_CONNECTED && millis() - wifiRetryAt > 5000) {
    WiFi.disconnect(); WiFi.begin(DRONE_WIFI_SSID, DRONE_WIFI_PASSWORD); wifiRetryAt = millis();
  }
  usbConsole(); readAx7020(); readTelemetry(); tickFlight();
  /* Prime the drone's UDP reply address after a fresh boot or reconnect. */
  static uint32_t lastPrimeAt = 0;
  if (WiFi.status() == WL_CONNECTED && !heartbeatFresh() && millis() - lastPrimeAt >= 1000) {
    uint8_t request[20] = {255, 255, droneSysId, 1};
    sendMavlink(20, 214, request, sizeof(request));
    lastPrimeAt = millis();
  }
  delay(10);
}

