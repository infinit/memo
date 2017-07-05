#include <memo/crash-report.hh>

#include <elle/log.hh>

ELLE_LOG_COMPONENT("CrashReporter");

#if MEMO_ENABLE_CRASH_REPORT

#include <string>

#include <boost/range/algorithm/max_element.hpp>

#include <elle/algorithm.hh>
#include <elle/bytes.hh>
#include <elle/log.hh>
#include <elle/log/FileLogger.hh>

#include <crash-report/CrashReporter.hh>

#include <memo/utility.hh> //canonical_folder, etc.

namespace memo
{
  using CrashReporter = crash_report::CrashReporter;

  /// Crash reporter.
  auto
  make_reporter()
  {
    auto const host = elle::os::getenv("MEMO_CRASH_REPORT_HOST", beyond());
    auto const url = elle::sprintf("%s/crash/report", host);

    auto const dumps_path = canonical_folder(xdg_cache_home() / "crashes");
    ELLE_DEBUG("dump to %s", dumps_path);

    auto const log_dir = canonical_folder(xdg_cache_home() / "logs");
    auto const log_base = (log_dir / "main.").string();

    // FIXME: Should be unique_ptr, but something in our handling of
    // reactor::Threads prevents it.
    auto res
      = std::make_shared<CrashReporter>(url, dumps_path, version_describe());
    res->make_payload = [log_dir, log_base] (auto const& base)
      {
        // Collect the existing numbers in logs/main.<NUM> file names.
        auto nums = std::vector<int>{};
        for (auto& p: bfs::directory_iterator(log_dir))
          if (auto n = elle::tail(p.path().string(), log_base))
            try
            {
              nums.emplace_back(std::stoi(*n));
            }
            catch (std::invalid_argument)
            {}
        auto i = boost::max_element(nums);
        if (i != end(nums))
        {
          // The log file next to the minidump file.
          auto const minilog = elle::print("{}.log", base);
          // Get the two last logs in the log directory, if they do
          // match (i.e., don't concatenate main.1 with main.3).
          auto&& o = std::ofstream(minilog);
          for (auto n: {*i - 1, *i})
          {
            auto const name = elle::print("{}{}", log_base, n);
            if (bfs::exists(name))
            {
              auto&& i = std::ifstream(name);
              o << i.rdbuf();
            }
          }
        }
      };
    return res;
  }

  /// Thread to send the crash reports (some might be pending from
  /// previous runs, saved on disk).
  ///
  /// This routine also creates the CrashReporter object which is in
  /// charge of catching errors and generating the minidumps.  So be
  /// sure to always create this object early enough.
  ///
  /// It should never cause the program to fail, catch all errors.
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("enabled");
    try
    {
      // Don't create the error handler within the thread, it might not
      // like that, and might be created too late to catch very early crashes.
      return std::make_unique<elle::reactor::Thread>("crash report",
        [cr = make_reporter()]
        {
          cr->upload_existing();
        });
    }
    catch (elle::Error const& e)
    {
      ELLE_LOG("cannot set up crash-handler: %s", e);
      return {};
    }
    catch (...)
    {
      ELLE_LOG("cannot set up crash-handler: %s", elle::exception_string());
      return {};
    }
  }
}

#else // ! MEMO_ENABLE_CRASH_REPORT

namespace memo
{
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("disabled");
    return {};
  }
}
#endif
