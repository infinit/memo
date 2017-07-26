#pragma once

#include <string>

#include <boost/filesystem.hpp>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// The directory for the critical logs (`~/.cache/infinit/memo/logs`).
  bfs::path log_dir();

  /// The log family name (`log_dir() / "main"`).
  std::string log_base();

  /// Set up the "critical" log, i.e., the log family typically stored
  /// in `~/.cache/infinit/memo/logs/main.*`.
  ///
  /// Reads `$MEMO_LOG_LEVEL`.
  void make_critical_log();

  /// The list of the @a n latest critical log files.
  std::vector<bfs::path> latest_logs(int n = 1);

  /// Generate a tarball with the @a n latest critical log files.
  void tar_logs(bfs::path const& tgz, int n = 1);
}
