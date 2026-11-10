#include "app_state.h"

namespace ignis {

static portMUX_TYPE s_snap_mux = portMUX_INITIALIZER_UNLOCKED;
static FlightSnapshot s_snap;

std::atomic<bool> g_flight_latched{false};
std::atomic<bool> g_post_fire{false};
std::atomic<bool> g_recovery_boot{false};
std::atomic<uint32_t> g_flight_loop_count{0};
BootInfo g_boot;

void snapshotPublish(const FlightSnapshot& s) {
  taskENTER_CRITICAL(&s_snap_mux);
  s_snap = s;
  taskEXIT_CRITICAL(&s_snap_mux);
}

void snapshotRead(FlightSnapshot& out) {
  taskENTER_CRITICAL(&s_snap_mux);
  out = s_snap;
  taskEXIT_CRITICAL(&s_snap_mux);
}

}  // namespace ignis
