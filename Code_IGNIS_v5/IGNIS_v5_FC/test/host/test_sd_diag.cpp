// Host tests: SD mount diagnostics (SdError mapping + diagnostic line format).
// Pure functions from src/core/sd_diag only. No Arduino, no SD card, no bus.
#include <cstring>
#include <string>
#include "mini_test.h"
#include "../../src/core/sd_diag.h"

using namespace ignis;

static SdMountSteps allOk() {
  SdMountSteps st;
  st.lock_ok = true;
  st.begin_ok = true;
  st.card_type = kSdCardSdhc;
  st.name_free = true;
  st.open_ok = true;
  return st;
}

TEST(sd_error_codes_are_stable) {
  // Printed on serial, in BOOTLOG.TXT and on the dashboard: never renumber.
  CHECK_EQ((int)SdError::NONE, 0);
  CHECK_EQ((int)SdError::SD_BEGIN_FAILED, 1);
  CHECK_EQ((int)SdError::CARD_TYPE_NONE, 2);
  CHECK_EQ((int)SdError::NAME_SPACE_EXHAUSTED, 3);
  CHECK_EQ((int)SdError::OPEN_FAILED, 4);
  CHECK_EQ((int)SdError::LOCK_TIMEOUT, 5);
  CHECK_EQ((int)SdError::WRITE_ERRORS, 6);
  CHECK_EQ((int)kSdErrorCount, 7);
}

TEST(sd_error_names_unique_and_match_enum) {
  CHECK(std::strcmp(sdErrorName(SdError::NONE), "NONE") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::SD_BEGIN_FAILED), "SD_BEGIN_FAILED") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::CARD_TYPE_NONE), "CARD_TYPE_NONE") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::NAME_SPACE_EXHAUSTED), "NAME_SPACE_EXHAUSTED") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::OPEN_FAILED), "OPEN_FAILED") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::LOCK_TIMEOUT), "LOCK_TIMEOUT") == 0);
  CHECK(std::strcmp(sdErrorName(SdError::WRITE_ERRORS), "WRITE_ERRORS") == 0);
  for (int i = 0; i < kSdErrorCount; i++)
    for (int j = i + 1; j < kSdErrorCount; j++)
      CHECK(std::strcmp(sdErrorName((SdError)i), sdErrorName((SdError)j)) != 0);
  CHECK(std::strcmp(sdErrorName((SdError)200), "UNKNOWN") == 0);   // out of range never crashes
}

TEST(sd_card_type_names) {
  CHECK(std::strcmp(sdCardTypeName(kSdCardNone), "NONE") == 0);
  CHECK(std::strcmp(sdCardTypeName(kSdCardMmc), "MMC") == 0);
  CHECK(std::strcmp(sdCardTypeName(kSdCardSd), "SD") == 0);
  CHECK(std::strcmp(sdCardTypeName(kSdCardSdhc), "SDHC") == 0);
  CHECK(std::strcmp(sdCardTypeName(kSdCardUnknown), "UNKNOWN") == 0);
  CHECK(std::strcmp(sdCardTypeName(kSdCardNotRead), "NOT_READ") == 0);
  CHECK(std::strcmp(sdCardTypeName(77), "INVALID") == 0);
}

TEST(sd_classify_all_steps_ok_is_none) {
  CHECK_EQ((int)sdClassifyMount(allOk()), (int)SdError::NONE);
  SdMountSteps st = allOk();
  st.card_type = kSdCardUnknown;   // an unrecognised but responding card still mounts
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::NONE);
}

TEST(sd_classify_each_single_failure) {
  SdMountSteps st = allOk(); st.lock_ok = false;
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::LOCK_TIMEOUT);
  st = allOk(); st.begin_ok = false;
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::SD_BEGIN_FAILED);
  st = allOk(); st.card_type = kSdCardNone;
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::CARD_TYPE_NONE);
  st = allOk(); st.name_free = false;
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::NAME_SPACE_EXHAUSTED);
  st = allOk(); st.open_ok = false;
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::OPEN_FAILED);
}

TEST(sd_classify_first_failed_step_wins) {
  // Defaults = nothing reached: reported as the earliest step (lock).
  SdMountSteps none;
  CHECK_EQ((int)sdClassifyMount(none), (int)SdError::LOCK_TIMEOUT);
  // The shapes sd_logger.cpp actually produces on early exit.
  SdMountSteps st; st.lock_ok = true;                       // SD.begin() failed
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::SD_BEGIN_FAILED);
  st.begin_ok = true; st.card_type = kSdCardNone;           // cardType() == CARD_NONE
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::CARD_TYPE_NONE);
  st.card_type = kSdCardSd;                                 // 1000 names all taken
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::NAME_SPACE_EXHAUSTED);
  st.name_free = true;                                      // open failed
  CHECK_EQ((int)sdClassifyMount(st), (int)SdError::OPEN_FAILED);
  // begin failed with card_type still NOT_READ must not be reported as CARD_TYPE_NONE.
  SdMountSteps b; b.lock_ok = true; b.card_type = kSdCardNotRead;
  CHECK_EQ((int)sdClassifyMount(b), (int)SdError::SD_BEGIN_FAILED);
}

TEST(sd_diag_line_exact_format_failure) {
  SdMountDiag d;
  d.err = SdError::SD_BEGIN_FAILED;
  d.attempt = 3;
  d.t_ms = 20123;
  d.cs_pin = 41;
  d.detect_raw = 1;
  d.card_type = kSdCardNotRead;
  d.spi_hz = 4000000UL;
  d.lock_held_ms = 2011;
  char buf[240];
  size_t n = sdFormatDiag("[SD]", d, buf, sizeof(buf));
  const char* want = "[SD] t=20123ms mount#3 err=1(SD_BEGIN_FAILED) cs=IO41 det_raw=1 "
                     "card=255(NOT_READ) hz=4000000 held_ms=2011 scan_ms=0 names=0";
  CHECK(std::strcmp(buf, want) == 0);
  CHECK_EQ(n, std::strlen(want));
}

TEST(sd_diag_line_exact_format_success) {
  SdMountDiag d;
  d.err = SdError::NONE;
  d.attempt = 1;
  d.t_ms = 812;
  d.cs_pin = 41;
  d.detect_raw = 0;
  d.card_type = kSdCardSdhc;
  d.spi_hz = 4000000UL;
  d.lock_held_ms = 96;
  d.scan_ms = 41;
  d.names_scanned = 18;
  char buf[240];
  sdFormatDiag("[SD]", d, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "[SD] t=812ms mount#1 err=0(NONE) cs=IO41 det_raw=0 card=3(SDHC) "
                         "hz=4000000 held_ms=96 scan_ms=41 names=18") == 0);
}

TEST(sd_diag_line_carries_required_fields_for_every_error) {
  // Phase 1 item 2: error code, SD_CS, raw SD_DETECT level, cardType, SD_SPI_HZ.
  for (int e = 0; e < kSdErrorCount; e++) {
    SdMountDiag d;
    d.err = (SdError)e;
    d.cs_pin = 41;
    d.detect_raw = e & 1;
    d.card_type = kSdCardSd;
    d.spi_hz = 4000000UL;
    char buf[240];
    CHECK(sdFormatDiag("[SD]", d, buf, sizeof(buf)) > 0);
    std::string s(buf);
    CHECK(s.find(std::string("err=") + std::to_string(e) + "(" + sdErrorName((SdError)e) + ")") != std::string::npos);
    CHECK(s.find(" cs=IO41 ") != std::string::npos);
    CHECK(s.find(std::string(" det_raw=") + std::to_string(e & 1) + " ") != std::string::npos);
    CHECK(s.find(" card=2(SD) ") != std::string::npos);
    CHECK(s.find(" hz=4000000 ") != std::string::npos);
    CHECK(s.find('\n') == std::string::npos);   // exactly one line
    CHECK(s.find('\r') == std::string::npos);
  }
}

TEST(sd_diag_line_never_asserts_card_presence) {
  // Card-detect polarity is unverified: the line reports the raw level only.
  for (int lvl = -1; lvl <= 1; lvl++) {
    SdMountDiag d;
    d.detect_raw = lvl;
    char buf[240];
    sdFormatDiag("[SD]", d, buf, sizeof(buf));
    std::string s(buf);
    CHECK(s.find("present") == std::string::npos);
    CHECK(s.find("absent") == std::string::npos);
    CHECK(s.find("inserted") == std::string::npos);
    CHECK(s.find(std::string("det_raw=") + std::to_string(lvl) + " ") != std::string::npos);
  }
}

TEST(sd_diag_bootlog_prefixes) {
  SdMountDiag d;
  d.err = SdError::CARD_TYPE_NONE;
  d.attempt = 1;
  d.cs_pin = 41;
  d.detect_raw = 1;
  d.card_type = kSdCardNone;
  d.spi_hz = 4000000UL;
  char buf[240];
  sdFormatDiag("sdfail first", d, buf, sizeof(buf));
  CHECK(std::strncmp(buf, "sdfail first t=0ms mount#1 err=2(CARD_TYPE_NONE) ", 49) == 0);
  sdFormatDiag("sdfail last n=7", d, buf, sizeof(buf));
  CHECK(std::strncmp(buf, "sdfail last n=7 t=0ms mount#1 ", 30) == 0);
  // A null prefix is tolerated (no crash, fields still present).
  CHECK(sdFormatDiag(nullptr, d, buf, sizeof(buf)) > 0);
  CHECK(std::strncmp(buf, " t=0ms ", 7) == 0);
}

TEST(sd_diag_line_truncation_and_extremes) {
  SdMountDiag d;
  d.err = SdError::NAME_SPACE_EXHAUSTED;
  d.attempt = 0xFFFFFFFFu;
  d.t_ms = 0xFFFFFFFFu;
  d.cs_pin = 255;
  d.detect_raw = -1;
  d.card_type = kSdCardUnknown;
  d.spi_hz = 0xFFFFFFFFu;
  d.lock_held_ms = 0xFFFFFFFFu;
  d.scan_ms = 0xFFFFFFFFu;
  d.names_scanned = 1000;
  char big[240];
  size_t n = sdFormatDiag("sdfail last n=4294967295", d, big, sizeof(big));
  CHECK(n > 0);
  CHECK(n < sizeof(big));                 // worst case fits sd_logger.cpp's 240-byte buffer
  CHECK(std::string(big).find("names=1000") != std::string::npos);
  CHECK(std::string(big).find("det_raw=-1 ") != std::string::npos);
  // Too small: reports 0 and leaves an empty string, never a cut line.
  char small_buf[40];
  std::memset(small_buf, 'x', sizeof(small_buf));
  CHECK_EQ(sdFormatDiag("[SD]", d, small_buf, sizeof(small_buf)), (size_t)0);
  CHECK_EQ((int)small_buf[0], 0);
  CHECK_EQ(sdFormatDiag("[SD]", d, nullptr, 10), (size_t)0);
  CHECK_EQ(sdFormatDiag("[SD]", d, small_buf, 0), (size_t)0);
}
