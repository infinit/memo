#pragma once

#include <regex>
#include <string>

#include <boost/filesystem.hpp>

#include <elle/flat-set.hh>
#include <elle/log/fwd.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// Our logs in memo rely on two concepts:
  ///
  /// - the "family" is a simple std::string such as "main", or
  /// "jmq/infinit/company".
  ///
  /// - the "base" (fs::path) is almost the log files name: it only lacks the
  /// generation (files are rotated as `{base}.0`, `{base}.1`, etc.).
  ///
  /// We have:
  ///   `{base} = {log_dir} / {family} / {now}-{pid}`,
  ///   `{log} = {base} . {version}`,
  ///   `{suffix} = {family} / {now}-{pid}`.

  /// The directory for the critical logs (`~/.cache/infinit/memo/logs`).
  bfs::path log_dir();

  /// The log base name (`log_dir() / family / {now}-{pid}`).
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

  /// Change the family of the main log.  Renames the current log file.
  void main_log_family(std::string const& family);

  /// Get the base of the (current) main log.
  ///
  /// For instance
  /// `/Users/bob/.cache/infinit/memo/logs/main/20170801T083249-32863`
  /// from
  /// `/Users/bob/.cache/infinit/memo/logs/main/20170801T083249-32863.0`
  bfs::path main_log_base();

  /// The @a n latest log files matching a regex.
  ///
  /// @param match  the regex.
  /// @param n      the maximum number of contiguous logs to gather.
  ///               0 for unlimited.
  ///
  /// @throws bfs::filesystem_error if for instance files are removed
  ///         during the directory traversal.
  std::vector<bfs::path>
  latest_logs(std::regex const& match, int n = 1);

  /// The @a n latest log files in a base.
  ///
  /// @param base  the log base name.  A period will be added.
  /// @param n     the maximum number of contiguous logs to gather.
  ///               0 for unlimited.
  ///
  /// @throws bfs::filesystem_error if for instance files are removed
  ///         during the directory traversal.
  std::vector<bfs::path>
  latest_logs(bfs::path const& base, int n = 1);

  /// The @a n latest log files in a family.
  ///
  /// @param family  the log family name.
  /// @param n     the maximum number of contiguous logs to gather.
  ///               0 for unlimited.
  ///
  /// @throws bfs::filesystem_error if for instance files are removed
  ///         during the directory traversal.
  std::vector<bfs::path>
  latest_logs_family(std::string const& family, int n = 1);

  /// The existing log families that match a pattern.
  ///
  /// @param match  a regex that applies only to the log suffix
  ///                 (i.e., not the `/home/...` part).
  boost::container::flat_set<std::string>
  log_families(std::regex const& match = {});

  /// Remove all the log files whose family (partially) matches a pattern.
  ///
  /// @param match  a regex, which matches anything by default.
  /// @param n      the number of the most recent files _not_ to remove.
  ///
  /// @throws bfs::filesystem_error if for instance files are removed
  ///         during the directory traversal.
  void log_remove(std::regex const& match = std::regex{""},
                  int n = 0);

  /// Generate a tgz with the latest matching log files.
  ///
  /// @param tgz    where the archive will be made.
  /// @param files  the log files to include.
  ///
  /// @return  the size of @a files.
  int
  tar_logs(bfs::path const& tgz,
           std::vector<bfs::path> const& files);
}
