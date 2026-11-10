// =============================================================================
// IGNIS v5: csv_row.h  (pure C++, host-testable)
// SD log row. Columns up to deploy_trigger keep the reference order
// (IGNIS_FC_heavy_13/DataLogger.h:52-59) so existing analysis scripts still
// line up. The reference's last column "resumed" is replaced by v5 columns.
// =============================================================================
#pragma once
#include <stddef.h>
#include "flight_types.h"

namespace ignis {

extern const char kCsvHeader[];
constexpr size_t kCsvRowMax = 512;

// Returns the number of chars written (excluding NUL), 0 on truncation.
size_t buildCsvRow(const FlightSnapshot& s, char* buf, size_t len);

}  // namespace ignis
