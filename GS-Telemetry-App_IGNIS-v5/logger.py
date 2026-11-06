"""
logger.py
=========
Per-session CSV logging for both telemetry packets and connection
events (connect / disconnect / port change), replacing the old
`open_csv_writer()` + inline `csv_writer.writerow()` calls.

One CSV file per application session, created on first write so an app
launch that never receives data doesn't leave an empty file behind.

Implements a buffer of 0.5 seconds to merge packets from both receivers 
(A and B) based on the sequence number (seq).
"""

from __future__ import annotations

import csv
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from config import LOG_DIR
from telemetry_parser import TelemetryFrame

# Header for the merged CSV file
MERGED_CSV_HEADER = [
    "datetime_utc", "seq", "flight_state",
    "A_rssi_dbm", "A_snr_db", "B_rssi_dbm", "B_snr_db", 
    "Merged_rssi_dbm", "Merged_snr_db",
    "agl_m", "abs_alt_m", "baro_temp_c", "baro_valid",
    "accel_x_g", "accel_y_g", "accel_z_g", "accel_total_g",
    "gyro_x_dps", "gyro_y_dps", "gyro_z_dps", "mpu_temp_c", "imu_valid",
    "battery_v",
    "flight_sd_state", "A_ground_sd_state", "B_ground_sd_state",
    "gps_fix", "gps_sats", "gps_lat", "gps_lon", "gps_alt_m",
    "pyro1_cont", "pyro2_cont", "pyro1_fired", "pyro2_fired",
    "tilt_deg", "boot_count", "reset_reason", "reset_reason_name", "deploy_trigger",
    "event_detail",
]

class SessionLogger:
    """Thread-safe CSV writer shared by both receiver threads."""

    def __init__(self, log_dir: Path = LOG_DIR) -> None:
        self._log_dir = log_dir
        self._lock = threading.Lock()
        self._file = None
        self._writer: Optional[csv.writer] = None
        self._path: Optional[Path] = None
        self._principal_receiver: Optional[str] = None
        self._pending_frames: dict[int, dict] = {}
    
    @property
    def path(self) -> Optional[Path]:
        return self._path

    def _ensure_open(self) -> None:
        if self._writer is not None:
            return
        self._log_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._path = self._log_dir / f"ignis_session_{ts}.csv"
        self._file = open(self._path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(MERGED_CSV_HEADER)
        self._file.flush()

    def log_event(self, receiver_id: str, event_type: str, detail: str = "") -> None:
        """Log a non-packet event: CONNECTED, DISCONNECTED, PORT_CHANGE, etc."""
        with self._lock:
            # The first to connect becomes the principal receiver
            if event_type == "CONNECTED" and self._principal_receiver is None:
                self._principal_receiver = receiver_id
                
            self._ensure_open()
            row = [""] * len(MERGED_CSV_HEADER)
            row[0] = datetime.now(timezone.utc).isoformat()
            row[-1] = f"[{receiver_id}] {event_type} {detail}"
            self._writer.writerow(row)
            self._file.flush()

    def start_new_flight(self, flight_number: int) -> None:
        """Create a flight boundary without closing or truncating the CSV."""
        with self._lock:
            self._ensure_open()
            for seq in sorted(list(self._pending_frames)):
                self._flush_seq(seq)
            row = [""] * len(MERGED_CSV_HEADER)
            row[0] = datetime.now(timezone.utc).isoformat()
            row[-1] = f"[SYSTEM] NEW_FLIGHT flight={flight_number}"
            self._writer.writerow(row)
            self._file.flush()
            
    def log_frame(self, frame: TelemetryFrame) -> None:
        """Receive packets and store them temporarily before writing them."""
        with self._lock:
            self._ensure_open()
            seq = frame.seq
            if seq is None: 
                return
            
            if seq not in self._pending_frames:
                self._pending_frames[seq] = {'time': time.time()}
            
            self._pending_frames[seq][frame.receiver_id] = frame
            
            # Empty the CSV for packets that exceed the 0.5 second wait time
            current_time = time.time()
            seqs_to_flush = [s for s, data in self._pending_frames.items() if current_time - data['time'] > 0.5]
            
            for s in sorted(seqs_to_flush):
                self._flush_seq(s)

    def _flush_seq(self, seq: int) -> None:
        """Unifies the data from A and B for a given sequence and writes the row."""
        data = self._pending_frames.pop(seq)
        frame_A = data.get("A")
        frame_B = data.get("B")
        
        # Determine the base frame using the principal receiver
        base_frame = None
        if self._principal_receiver == "A" and frame_A: 
            base_frame = frame_A
        elif self._principal_receiver == "B" and frame_B: 
            base_frame = frame_B
            
        # Backup: if the principal didn't receive this packet, use the one that did
        if base_frame is None:
            base_frame = frame_A if frame_A else frame_B
            
        if base_frame is None: 
            return 
            
        a_rssi = frame_A.rssi_dbm if frame_A else ""
        a_snr = frame_A.snr_db if frame_A else ""
        b_rssi = frame_B.rssi_dbm if frame_B else ""
        b_snr = frame_B.snr_db if frame_B else ""
        flight_sd_state = next(
            (
                candidate.flight_sd_state
                for candidate in (frame_A, frame_B)
                if candidate is not None and candidate.flight_sd_state is not None
            ),
            "",
        )

        row = [
            base_frame.received_at.isoformat(),
            base_frame.seq,
            base_frame.flight_state,
            a_rssi, a_snr,
            b_rssi, b_snr,
            base_frame.rssi_dbm,  # Merged_rssi
            base_frame.snr_db,    # Merged_snr
            base_frame.agl_m,
            base_frame.abs_alt_m,
            base_frame.baro_temp_c,
            base_frame.baro_valid,
            base_frame.accel_x_g,
            base_frame.accel_y_g,
            base_frame.accel_z_g,
            base_frame.accel_total_g,
            base_frame.gyro_x_dps,
            base_frame.gyro_y_dps,
            base_frame.gyro_z_dps,
            base_frame.mpu_temp_c,
            base_frame.imu_valid,
            base_frame.battery_v,
            flight_sd_state,
            frame_A.ground_sd_state if frame_A else "",
            frame_B.ground_sd_state if frame_B else "",
            base_frame.gps_fix,
            base_frame.gps_sats,
            base_frame.gps_lat,
            base_frame.gps_lon,
            base_frame.gps_alt_m,
            base_frame.pyro1_cont,
            base_frame.pyro2_cont,
            base_frame.pyro1_fired,
            base_frame.pyro2_fired,
            base_frame.tilt_deg,
            base_frame.boot_count,
            base_frame.reset_reason,
            base_frame.reset_reason_name,
            base_frame.deploy_trigger,
            "",
        ]
        self._writer.writerow(row)
        self._file.flush()

    def close(self) -> None:
        """Forces flushing of all pending frames and closes the CSV file."""
        with self._lock:
            for seq in sorted(self._pending_frames.keys()):
                self._flush_seq(seq)
            if self._file is not None:
                self._file.close()
                self._file = None
                self._writer = None
