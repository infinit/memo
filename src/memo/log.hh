#pragma once

#include <string>

#include <boost/filesystem.hpp>

#include <elle/flat-set.hh>
#include <elle/log/fwd.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// Our logs in memo rely on two concepts:
  ///
  /// - the "family" is a simple string such as "main", or
  /// "jmq/infinit/company".
  ///
  /// - the "base" is almost the log files name: it only lacks the
  /// generation (files are rotated as `{base}.0`, `{base}.1`, etc.).
  ///
  /// We have `{base} = {log_dir} / {family} / {now}-{pid}`,
  /// and `{log} = {base} . {version}.

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
  /// Beware that std::string is implicitely convertable into
  /// bfs::path, so it is very dangerous to rely on overloading here.
  /// And we avoid regex to avoid adding more #includes in this
  /// header, but it might be a better solution anyway.
  std::vector<bfs::path>
  latest_logs_match(std::string const& match, int n = 1);

  /// The @a n latest log files in a base.
  ///
  /// @param base  the log base name.  A period will be added.
  /// @param n     the maximum number of contiguous logs to gather.
  ///               0 for unlimited.
  std::vector<bfs::path>
  latest_logs(bfs::path const& base, int n = 1);

  /// The existing log families that match a pattern.
  ///
  /// @param pattern  a regex that applies only to the log suffix
  ///                 (i.e., not the `/home/...` part).
  boost::container::flat_set<std::string>
  log_families(std::string const& pattern = {});

  /// Remove all the log files whose family match a pattern.
  ///
  /// @param pattern  a regex
  void log_remove(std::string const& pattern = {});

  /// Generate a tgz with the latest matching log files.
  ///
  /// @param tgz    where the archive will be made.
  /// @param match  a regex that applies to the log suffix
  /// @param n      the maximum number of contiguous logs to gather.
  ///
  /// @return  whether the tarball was created (i.e., there are
  ///          logs under that base name).
  int tar_logs_match(bfs::path const& tgz,
                     std::string const& match, int n = 1);

  /// Generate a tgz with the latest critical log files.
  ///
  /// @param tgz    where the archive will be made.
  /// @param base   the log base name.  A period will be added.
  /// @param n      the maximum number of contiguous logs to gather.
  ///
  /// @return  whether the tarball was created (i.e., there are
  ///          logs under that base name).
  int tar_logs(bfs::path const& tgz,
               bfs::path const& base, int n = 1);
}
