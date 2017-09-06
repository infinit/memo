#include <memo/log.hh>

#include <regex>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/partial_sort.hpp>
#include <boost/range/irange.hpp>

#include <elle/archive/archive.hh>
#include <elle/fstream.hh> // rotate_versions.
#include <elle/log.hh>
#include <elle/log/FileLogger.hh>
#include <elle/system/getpid.hh>

#include <memo/utility.hh>

ELLE_LOG_COMPONENT("memo.log");

namespace memo
{
  bfs::path log_dir()
  {
    auto const d = memo::getenv("LOG_DIR",
                                (xdg_cache_home() / "logs").string());
    return canonical_folder(d);
  }

  std::string log_base(std::string const& family)
  {
    auto const now = elle::to_iso8601_local(std::chrono::system_clock::now());
    auto const base
      = elle::print("{family}/{now}-{pid}",
                    {
                      {"family", family},
                      {"now",    now},
                      {"pid",    elle::system::getpid()},
                    });
    auto const path = log_dir() / base;
    // log_dir is created, but base may also contain `/`.
    elle::create_parent_directories(path);
    return path.string();
  }

  namespace
  {
    /// Prune the log_dir from p.
    bfs::path
    log_suffix(bfs::path const& p)
    {
      ELLE_ASSERT(boost::starts_with(p, log_dir()));
      // Unfortunately we can't erase_head_copy on path components:
      // bfs and boost::erase_head_copy are incompatible.
      return bfs::path{boost::erase_head_copy(p.string(),
                                              log_dir().string().size() + 1)};
    }

    /// Get the family from a log file.
    std::string
    log_family(bfs::path const& p)
    {
      // The family of a log file is just the directory name.
      return log_suffix(p).parent_path().string();
    }

    /// Whether a directory entry's name is alike
    /// "~/.cache/infinit/memo/logs/foo/base.123".
    auto
    has_version(bfs::directory_entry const& e)
    {
      auto const name = e.path().string().substr(log_dir().size() + 1);
      auto const dot = name.rfind('.');
      return dot != name.npos
        && dot != name.size()
        && std::all_of(name.begin() + dot + 1, name.end(),
                       boost::is_digit());
    }

    /// All the log files whose path name match a given regex (not
    /// including the log_dir).
    ///
    /// Beware that incrementing the iterator for the returned
    /// generator might throw, if, for instance, files are removed
    /// while we scan the directory.
    auto
    log_files(std::regex const& re)
    {
      using namespace boost::adaptors;
      return bfs::recursive_directory_iterator(log_dir())
        | filtered(is_visible_file)
        | filtered(has_version)
        | filtered([re](auto const& p)
                   {
                     return regex_search(log_suffix(p).string(), re);
                   });
    }

    // FIXME: the rvalue implementation of elle::make_vector and these
    // ranges don't work together.
    template <typename Paths>
    auto
    to_vector(Paths const& ps)
    {
      auto res = std::vector<bfs::path>{};
      for (auto const& p: ps)
        res.emplace_back(p.path());
      return res;
    }

    std::unique_ptr<elle::log::Logger>
    make_log(std::string const& family)
    {
      auto const level = ("*athena*:DEBUG"
                          ",*cli*:DEBUG"
                          ",*model*:DEBUG"
                          ",*grpc*:DEBUG"
                          ",*prometheus:LOG");
      auto const spec =
        elle::print("file://{base}"
                    "?"
                    "var=MEMO_LOG_LEVEL,"
                    "time,microsec,"
                    "size=64MiB,rotate=15,"
                    "{level}",
                    {
                      {"base", log_base(family)},
                      {"level", level},
                    });
      ELLE_DUMP("building log: {}", spec);
      auto res = elle::log::make_logger(spec);
      auto const dashes = std::string(80, '-') + '\n';
      res->message(elle::log::Logger::Level::log,
                   elle::log::Logger::Type::warning,
                   _trace_component_,
                   dashes + dashes + dashes
                   + "starting memo " + version_describe(),
                   __FILE__, __LINE__, "Memo::Memo");
      return res;
    }
  }

  elle::log::FileLogger*&
  main_log()
  {
    static auto res = static_cast<elle::log::FileLogger*>(nullptr);
    return res;
  }

  void make_main_log()
  {
    // There is (currently) no risk of concurrency.  If needed in the
    // future, use a mutex.
    if (!main_log())
    {
      // Keep at most 15 main logs.
      try
      {
        log_remove(std::regex{"^main/"}, 15);
      }
      catch (bfs::filesystem_error)
      {}
      auto l = make_log("main");
      main_log() = dynamic_cast<elle::log::FileLogger*>(l.get());
      elle::log::logger_add(std::move(l));
    }
  }

  bfs::path
  main_log_base()
  {
    return elle::base(main_log()->fstream().path());
  }

  void
  main_log_family(std::string const& family)
  {
    // Keep at most 15 logs in this family.
    log_remove(std::regex{"^" + family + "/"}, 15);

    // Compute the new base without calling log_base, as we don't want
    // to change the timestamp for instance.
    auto const fname = main_log()->fstream().path().stem();
    auto const new_base = log_dir() / family / fname;
    elle::create_parent_directories(new_base);
    main_log()->base(new_base);
  }

  std::vector<bfs::path>
  latest_logs(std::regex const& match, int n)
  {
    auto paths = [&]
      {
        try
        {
          return to_vector(log_files(match));
        }
        catch (bfs::filesystem_error const& e)
        {
          ELLE_WARN("cannot gather logs: {}"
                    " (maybe logs were removed by another process)",
                    e);
          throw;
        }
      }();
    auto const begin = paths.begin();
    auto const size = int(paths.size());
    auto const num = n ? std::min(n, size) : size;
    auto const last = std::next(begin, num);
    boost::partial_sort(paths, last);
    return {begin, last};
  }

  std::vector<bfs::path>
  latest_logs(bfs::path const& base, int n)
  {
    // The greatest NUM in logs/main.<NUM> file names.
    auto const last = [&base]() -> boost::optional<int>
      {
        auto const nums = elle::rotate_versions(base);
        if (nums.empty())
          return {};
        else
          return *boost::max_element(nums);
      }();
    auto res = std::vector<bfs::path>{};
    if (last)
    {
      // Get the `n` latest logs in the log directory, if they are
      // consecutive (i.e., don't take main.1 with main.3).
      for (auto i: boost::irange(*last, *last - n, -1))
      {
        auto const name = elle::print("{}.{}", base.string(), i);
        if (bfs::exists(name))
          res.emplace_back(name);
        else
          break;
      }
    }
    return res;
  }

  std::vector<bfs::path>
  latest_logs_family(std::string const& family, int n)
  {
    return latest_logs(std::regex{"^" + family + "/"}, n);
  }

  boost::container::flat_set<std::string>
  log_families(std::regex const& match)
  {
    auto res = boost::container::flat_set<std::string>{};
    for (auto const& p: log_files(match))
      res.emplace(log_family(p));
    return res;
  }

  void
  log_remove(std::regex const& match, int n)
  {
    using namespace boost::adaptors;
    // All the logs, sorted by increasing creation date.
    auto const logs = latest_logs(match, 0);
    auto const size = int(logs.size());
    if (n < size)
      for (auto const& p: logs | sliced(0, size - n))
        elle::try_remove(p);
  }

  int
  tar_logs(bfs::path const& tgz,
           std::vector<bfs::path> const& files)
  {
    if (files.empty())
      ELLE_LOG("there are no log files");
    else
      {
        ELLE_DUMP("generating {} containing {}", tgz, files);
        archive(elle::archive::Format::tar_gzip, files, tgz);
      }
    return files.size();
  }
}
