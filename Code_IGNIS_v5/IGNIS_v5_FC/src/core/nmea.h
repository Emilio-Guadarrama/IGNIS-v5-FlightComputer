// =============================================================================
// IGNIS v5: nmea.h  (pure C++, host-testable)
// Minimal NMEA 0183 parser for the SAM-M10Q: GGA (fix quality, sats, position,
// altitude) and RMC (validity, position). Checksum enforced: a sentence with a
// bad or missing checksum is discarded. Replaces TinyGPSPlus (not installed,
// see CHANGES.md). Feed bytes one at a time, no allocation.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

struct GpsFix {
  bool   fix = false;        // GGA quality > 0 (and RMC 'A' when seen)
  double lat = 0, lon = 0;   // degrees, south/west negative
  float  alt_m = 0;          // MSL altitude from GGA
  uint8_t sats = 0;
  uint32_t sentences_ok = 0;
  uint32_t checksum_errors = 0;
};

class NmeaParser {
 public:
  // Returns true when a complete, checksum-valid GGA or RMC updated `fix()`.
  bool feed(char c);
  const GpsFix& fix() const { return fix_; }

 private:
  bool finish();
  void parseGga(char* fields[], int n);
  void parseRmc(char* fields[], int n);

  static constexpr int kMax = 100;
  char buf_[kMax + 1] = {};
  int len_ = 0;
  bool in_sentence_ = false;
  GpsFix fix_;
};

// Exposed for tests: "ddmm.mmmm" + hemisphere -> signed degrees. false on garbage.
bool nmeaToDegrees(const char* value, char hemi, double& out);

}  // namespace ignis
