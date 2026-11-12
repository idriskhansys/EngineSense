#pragma once
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "enginesense/can_frame.hpp"
#include "enginesense/fault.hpp"

namespace enginesense {

/// SocketCAN `candump -L` text format: `(1699999999.123456) can0 0CF00400#00112233445566AA`.
/// Timestamps are stored relative to the first frame. Parsing never throws; bad lines are counted.
struct LogReadResult {
  std::vector<CanFrame> frames;
  std::size_t bad_lines = 0;
  std::size_t first_bad_line = 0;  ///< 1-based, 0 if none
};

std::string format_log_line(const CanFrame& f, const char* iface = "can0", std::uint64_t base_epoch_s = 1'700'000'000);
void write_log(std::ostream& os, const std::vector<CanFrame>& frames);
bool write_log_file(const std::string& path, const std::vector<CanFrame>& frames);

LogReadResult read_log(std::istream& is);
bool read_log_file(const std::string& path, LogReadResult& out);

/// CSV: id,type,severity,key,signal,raised_us,cleared_us,active,value,message
void write_fault_csv(std::ostream& os, const std::vector<FaultNotification>& notes);

}  // namespace enginesense
