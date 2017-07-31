#include <memo/log.hh>

#include <regex>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/max_element.hpp>
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
    auto const now =
      to_iso_string(boost::posix_time::second_clock::universal_time());
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
    std::unique_ptr<elle::log::Logger>
    make_log(std::string const& family)
    {
      auto const level =
        memo::getenv("LOG_LEVEL",
                     "*athena*:DEBUG,*cli*:DEBUG,*model*:DEBUG"
                     ",*grpc*:DEBUG,*prometheus:LOG"s);
      auto const spec =
        elle::print("file://{file}"
                    "?"
                    "time,microsec,"
                    "size=64MiB,rotate=15,"
                    "{level}",
                    {
                      {"file", log_base(family)},
                      {"level", level},
                    });
      ELLE_DUMP("building log: {}", spec);
      auto logger = elle::log::make_logger(spec);
      auto const dashes = std::string(80, '-') + '\n';
      logger->message(elle::log::Logger::Level::log,
                      elle::log::Logger::Type::warning,
                      _trace_component_,
                      dashes + dashes + dashes
                      + "starting memo " + version_describe(),
                      __FILE__, __LINE__, "Memo::Memo");
      return logger;
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
    auto l = make_log("main");
    main_log() = dynamic_cast<elle::log::FileLogger*>(l.get());
    elle::log::logger_add(std::move(l));
  }

  void
  main_log_base(std::string const& family)
  {
    auto const old_fam = elle::base(main_log()->fstream().path());
    auto const new_fam = boost::replace_last_copy(old_fam.string(), "main", family);
    elle::create_parent_directories(new_fam);
    main_log()->base(new_fam);
  }

  std::vector<bfs::path>
  latest_logs(std::string const& family, int n)
  {
    // The greatest NUM in logs/main.<NUM> file names.
    auto const last = [&family]() -> boost::optional<int>
      {
        auto const nums = elle::rotate_versions(log_base(family));
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
        auto const name = elle::print("{}.{}", log_base(family), i);
        if (bfs::exists(name))
          res.emplace_back(name);
        else
          break;
      }
    }
    return res;
  }

  namespace
  {
    /// Prune the log_dir from p.
    auto
    log_suffix(bfs::path const& p)
    {
      ELLE_ASSERT(boost::starts_with(p, log_dir()));
      // Unfortunately we can't erase_head_copy on path components:
      // bfs and boost::erase_head_copy are incompatible.
      return bfs::path{boost::erase_head_copy(p.string(),
                                              log_dir().string().size() + 1)};
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
    auto
    log_files(std::string const& re)
    {
      using namespace boost::adaptors;
      return bfs::recursive_directory_iterator(log_dir())
        | filtered(is_visible_file)
        | filtered(has_version)
        | filtered([re = std::regex(re)](auto const& p)
                   {
                     return regex_search(log_suffix(p).string(), re);
                   });
    }
  }

  boost::container::flat_set<std::string>
  log_families(std::string const& re)
  {
    auto res = boost::container::flat_set<std::string>{};
    for (auto const& p: log_files(re))
      // The family of a log file is just the directory name.
      res.emplace(log_suffix(p.path()).parent_path().string());
    return res;
  }

  void
  log_remove(std::string const& re)
  {
    // FIXME: the rvalue implementation of elle::make_vector and these
    // ranges don't work together.
    auto const paths = [&]
      {
        auto res = std::vector<bfs::path>{};
        for (auto const& p: log_files(re))
          res.emplace_back(p.path());
        return res;
      }();
    for (auto const& p: paths)
      elle::try_remove(p);
  }

  bool tar_logs(bfs::path const& tgz, std::string const& family, int n)
  {
    auto const files = latest_logs(family, n);
    if (files.empty())
    {
      ELLE_LOG("there are no log files");
      return false;
    }
    else
    {
      ELLE_DUMP("generating {} containing {}", tgz, files);
      archive(elle::archive::Format::tar_gzip, files, tgz);
      return true;
    }
  }
}
