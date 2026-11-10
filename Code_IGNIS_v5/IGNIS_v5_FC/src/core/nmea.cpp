#include "nmea.h"
#include <stdlib.h>
#include <string.h>

namespace ignis {

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool nmeaToDegrees(const char* v, char hemi, double& out) {
  if (!v || !*v) return false;
  char* end = nullptr;
  double raw = strtod(v, &end);
  if (end == v || raw < 0) return false;
  int deg = (int)(raw / 100.0);
  double minutes = raw - deg * 100.0;
  if (minutes >= 60.0) return false;
  double d = deg + minutes / 60.0;
  if (hemi == 'S' || hemi == 'W') d = -d;
  else if (hemi != 'N' && hemi != 'E') return false;
  out = d;
  return true;
}

bool NmeaParser::feed(char c) {
  if (c == '$') { in_sentence_ = true; len_ = 0; return false; }
  if (!in_sentence_) return false;
  if (c == '\r' || c == '\n') { in_sentence_ = false; return finish(); }
  if (len_ >= kMax) { in_sentence_ = false; return false; }  // overlong: drop
  buf_[len_++] = c;
  return false;
}

bool NmeaParser::finish() {
  buf_[len_] = '\0';
  char* star = strchr(buf_, '*');
  if (!star || star[1] == '\0' || star[2] == '\0') { fix_.checksum_errors++; return false; }
  int hi = hexVal(star[1]), lo = hexVal(star[2]);
  if (hi < 0 || lo < 0) { fix_.checksum_errors++; return false; }
  uint8_t sum = 0;
  for (char* p = buf_; p < star; p++) sum ^= (uint8_t)*p;
  if (sum != (uint8_t)(hi * 16 + lo)) { fix_.checksum_errors++; return false; }
  *star = '\0';

  // Split on commas, keeping empty fields.
  char* fields[24];
  int n = 0;
  char* p = buf_;
  fields[n++] = p;
  while (*p && n < 24) {
    if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
    p++;
  }
  const char* id = fields[0];
  if (strlen(id) != 5) return false;
  if (strcmp(id + 2, "GGA") == 0) { parseGga(fields, n); fix_.sentences_ok++; return true; }
  if (strcmp(id + 2, "RMC") == 0) { parseRmc(fields, n); fix_.sentences_ok++; return true; }
  return false;
}

// $GxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,...
void NmeaParser::parseGga(char* f[], int n) {
  if (n < 10) return;
  int quality = atoi(f[6]);
  fix_.sats = (uint8_t)atoi(f[7]);
  if (quality > 0) {
    double lat, lon;
    if (nmeaToDegrees(f[2], f[3][0], lat) && nmeaToDegrees(f[4], f[5][0], lon)) {
      fix_.lat = lat;
      fix_.lon = lon;
      if (f[9][0]) fix_.alt_m = (float)atof(f[9]);
      fix_.fix = true;
      return;
    }
  }
  fix_.fix = false;  // last-known position is kept, fix flag drops
}

// $GxRMC,time,status,lat,N,lon,E,...
void NmeaParser::parseRmc(char* f[], int n) {
  if (n < 7) return;
  if (f[2][0] != 'A') { fix_.fix = false; return; }
  double lat, lon;
  if (nmeaToDegrees(f[3], f[4][0], lat) && nmeaToDegrees(f[5], f[6][0], lon)) {
    fix_.lat = lat;
    fix_.lon = lon;
  }
}

}  // namespace ignis
