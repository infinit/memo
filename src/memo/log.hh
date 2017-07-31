#pragma once

#include <string>

#include <boost/filesystem.hpp>

#include <elle/flat-set.hh>
#include <elle/log/fwd.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// The directory for the critical logs (`~/.cache/infinit/memo/logs`).
  bfs::path log_dir();

  /// The log family name (`log_dir() / family`).
  ///
  /// Ensures that the parent directory exists.
  std::string log_base(std::string const& family = "main");

  /// The main logger.
  elle::log::FileLogger*& main_log();

  /// Set up the main log, i.e., the log family typically stored in
  /// `~/.cache/infinit/memo/logs/main.*`.  Same as
  /// `make_log("main")`.
  ///
  /// Reads `$MEMO_LOG_LEVEL`.
  void make_main_log();

  /// Change the family of the main log.
  void main_log_base(std::string const& family);

  /// The list of the @a n latest log files in a family.
  ///
  /// @param family  the family base name.  A period will be added.
  /// @param n     the maximum number of contiguous logs to gather.
  std::vector<bfs::path>
  latest_logs(std::string const& family = "main", int n = 1);

  /// The existing log families that match a pattern.
  ///
  /// @param pattern  a regex
  boost::container::flat_set<std::string>
  log_families(std::string const& pattern = {});

  /// Remove all the log files whose family match a pattern.
  ///
  /// @param pattern  a regex
  void log_remove(std::string const& pattern = {});

  /// Generate a tgz with the latest critical log files.
  ///
  /// @param tgz    where the archive will be made.
  /// @param family the family base name.  A period will be added.
  /// @param n      the maximum number of contiguous logs to gather.
  ///
  /// @return  whether the tarball was created (i.e., there are
  ///          logs under that base name).
  bool tar_logs(bfs::path const& tgz,
                std::string const& family = "main", int n = 1);
}
