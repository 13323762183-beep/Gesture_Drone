#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

struct CommandPacket {
  uint32_t seq;
  char action[12];
  unsigned raw;
  unsigned stable;
  int x;
  int y;
};

static uint16_t commandCrc(const char *s, size_t n) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < n; ++i) {
    crc ^= (uint16_t)(uint8_t)s[i] << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

static bool commandParse(const char *s, CommandPacket &out) {
  const char *star = strchr(s, '*');
  if (!star || strlen(star + 1) != 4) return false;
  for (int i = 1; i <= 4; ++i) if (!isxdigit((unsigned char)star[i])) return false;
  if (commandCrc(s, (size_t)(star - s)) != strtoul(star + 1, nullptr, 16)) return false;

  unsigned long seq;
  int used = 0;
  if (sscanf(s, "CMD1,%lu,%11[A-Z],%u,%u,%d,%d%n", &seq, out.action,
             &out.raw, &out.stable, &out.x, &out.y, &used) != 6 || s + used != star)
    return false;
  if (seq > UINT32_MAX || out.raw > 3 || out.stable > 3) return false;
  out.seq = (uint32_t)seq;
  const char *a = out.action;
  if (!strcmp(a,"UNLOCK")) return out.raw == 1 && out.stable == 1;
  if (!strcmp(a,"LAND")) return out.raw == 3 && out.stable == 3;
  if (!strcmp(a,"HOLD") || !strcmp(a,"FORWARD") || !strcmp(a,"BACK") || !strcmp(a,"LEFT") || !strcmp(a,"RIGHT"))
    return out.raw == 2 && out.stable == 2 && out.x >= 0 && out.x < 96 && out.y >= 0 && out.y < 72;
  if (strcmp(a,"STOP") && strcmp(a,"CALIBRATING")) return false;
  return true;
}

static bool commandProtocolSelfTest() {
  const char *payload = "CMD1,7,FORWARD,2,2,48,20";
  char line[80];
  snprintf(line, sizeof(line), "%s*%04X", payload, commandCrc(payload, strlen(payload)));
  CommandPacket p = {};
  if (!commandParse(line, p) || p.seq != 7 || strcmp(p.action, "FORWARD") || p.x != 48) return false;
  line[5] = '8';
  return !commandParse(line, p);
}

