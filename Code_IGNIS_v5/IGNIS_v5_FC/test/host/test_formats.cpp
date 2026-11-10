// Host tests: telemetry packet layout, RadioHead framing, NMEA, CSV row.
#include <cstring>
#include <string>
#include "mini_test.h"
#include "../../src/core/csv_row.h"
#include "../../src/core/nmea.h"
#include "../../src/core/telemetry_packet.h"

using namespace ignis;

TEST(packet_is_98_bytes_and_little_endian_layout) {
  CHECK_EQ(sizeof(TelemetryPacket), (size_t)98);
  FlightSnapshot s;
  s.state = FlightState::RECOVERY;
  s.reset_reason = 9;
  s.boot_count = 0x1234;
  s.deploy_trigger = 2;
  s.cont = {true, false, true};
  s.sd = SdStatus::LOGGING;
  s.battery_v = 8.25f;
  TelemetryPacket p;
  encodeTelemetry(s, 0xA1B2C3D4u, p);
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&p);
  CHECK_EQ((int)b[0], 0xD4);            // seq LE
  CHECK_EQ((int)b[8], 8);               // state = RECOVERY
  CHECK_EQ((int)b[85], 1);              // pyro1_continuity
  CHECK_EQ((int)b[86], 0);
  CHECK_EQ((int)b[89], 1);              // sd_logging
  CHECK_EQ((int)b[94], 0x34);           // boot_count LE
  CHECK_EQ((int)b[95], 0x12);
  CHECK_EQ((int)b[96], 9);              // reset_reason
  CHECK_EQ((int)b[97], 2);              // deploy_trigger
  float v; std::memcpy(&v, b + 81, 4);
  CHECK_NEAR(v, 8.25, 1e-6);
}

TEST(packet_invalid_continuity_reports_zero) {
  FlightSnapshot s;
  s.cont = {true, true, false};
  TelemetryPacket p;
  encodeTelemetry(s, 0, p);
  CHECK_EQ((int)p.pyro1_continuity, 0);
  CHECK_EQ((int)p.pyro2_continuity, 0);
}

TEST(radiohead_frame_header) {
  TelemetryPacket p{};
  p.seq = 7;
  uint8_t buf[120];
  CHECK_EQ(buildRadioHeadFrame(p, buf, sizeof(buf)), (size_t)102);
  CHECK_EQ((int)buf[0], 0xFF);
  CHECK_EQ((int)buf[1], 0xFF);
  CHECK_EQ((int)buf[2], 0);
  CHECK_EQ((int)buf[3], 0);
  CHECK_EQ((int)buf[4], 7);
  CHECK_EQ(buildRadioHeadFrame(p, buf, 50), (size_t)0);
}

static void feedAll(NmeaParser& p, const char* s) { while (*s) p.feed(*s++); }

TEST(nmea_gga_fix_parsed) {
  // Checksum computed here rather than hand-typed.
  NmeaParser q;
  const char* body = "GNGGA,123519,2040.1234,N,10324.5678,W,1,09,0.9,1580.4,M,-10.0,M,,";
  unsigned sum = 0; for (const char* c = body; *c; c++) sum ^= (unsigned char)*c;
  char line[160]; std::snprintf(line, sizeof(line), "$%s*%02X\r\n", body, sum);
  feedAll(q, line);
  CHECK(q.fix().fix);
  CHECK_EQ((int)q.fix().sats, 9);
  CHECK_NEAR(q.fix().lat, 20.0 + 40.1234 / 60.0, 1e-9);
  CHECK_NEAR(q.fix().lon, -(103.0 + 24.5678 / 60.0), 1e-9);
  CHECK_NEAR(q.fix().alt_m, 1580.4, 1e-3);
}

TEST(nmea_bad_checksum_rejected) {
  NmeaParser p;
  feedAll(p, "$GNGGA,123519,2040.1234,N,10324.5678,W,1,09,0.9,1580.4,M,-10.0,M,,*00\r\n");
  CHECK(!p.fix().fix);
  CHECK_EQ((int)p.fix().checksum_errors, 1);
  feedAll(p, "$GNGGA,123519,2040.1234,N,10324.5678,W,1,09,0.9,1580.4,M,-10.0,M,,\r\n");
  CHECK_EQ((int)p.fix().checksum_errors, 2);  // missing checksum
}

TEST(nmea_no_fix_and_garbage) {
  NmeaParser p;
  const char* body = "GPGGA,000000,,,,,0,00,99.9,,M,,M,,";
  unsigned sum = 0; for (const char* c = body; *c; c++) sum ^= (unsigned char)*c;
  char line[120]; std::snprintf(line, sizeof(line), "$%s*%02X\r\n", body, sum);
  feedAll(p, line);
  CHECK(!p.fix().fix);
  std::string junk(500, 'A');  // overlong line must not overflow
  feedAll(p, ("$" + junk + "\n").c_str());
  CHECK(!p.fix().fix);
  double d;
  CHECK(!nmeaToDegrees("abc", 'N', d));
  CHECK(!nmeaToDegrees("2075.0", 'N', d));  // minutes >= 60
  CHECK(!nmeaToDegrees("2040.0", 'X', d));
}

TEST(csv_row_column_count_matches_header) {
  FlightSnapshot s;
  s.gps_lat = -12.345678; s.gps_lon = 123.456789;
  char row[kCsvRowMax];
  size_t n = buildCsvRow(s, row, sizeof(row));
  CHECK(n > 0);
  auto commas = [](const char* t) { int c = 0; for (; *t; t++) c += (*t == ','); return c; };
  CHECK_EQ(commas(row), commas(kCsvHeader));
  CHECK_EQ(buildCsvRow(s, row, 20), (size_t)0);  // truncation detected
}
