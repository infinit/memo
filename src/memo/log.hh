#pragma once

#include <string>

#include <boost/filesystem.hpp>

#include <elle/flat-set.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// The directory for the critical logs (`~/.cache/infinit/memo/logs`).
  bfs::path log_dir();

  /// The log family name (`log_dir() / base`).
  ///
  /// Ensures that the parent directory exists.
  std::string log_base(std::string const& base = "main");

  /// Set up rotating logs for a given base.
  ///
  /// Reads `$MEMO_LOG_LEVEL`.
  void make_log(std::string const& base);

  /// Set up the main log, i.e., the log family typically stored in
  /// `~/.cache/infinit/memo/logs/main.*`.  Same as
  /// `make_log("main")`.
  ///
  /// Reads `$MEMO_LOG_LEVEL`.
  void make_main_log();

  /// The list of the @a n latest log files in a family.
  ///
  /// @param base  the family base name.  A period will be added.
  /// @param n     the maximum number of contiguous logs to gather.
  std::vector<bfs::path>
  latest_logs(std::string const& base = "main", int n = 1);

  /// The existing log families of existing logs.
  boost::container::flat_set<std::string> log_families();

  /// Remove all the log files.
  void log_remove_all();

  /// Generate a tgz with the latest critical log files.
  ///
  /// @param tgz   where the archive will be made.
  /// @param base  the family base name.  A period will be added.
  /// @param n     the maximum number of contiguous logs to gather.
  ///
  /// @return  whether the tarball was created (i.e., there are
  ///          logs under that base name).
  bool tar_logs(bfs::path const& tgz,
                std::string const& base = "main", int n = 1);
}
