#include <memo/crash-report.hh>

#include <elle/assert.hh>
#include <elle/log.hh>

ELLE_LOG_COMPONENT("memo.crash-report");



#if MEMO_ENABLE_CRASH_REPORT

#include <string>

#include <boost/range/algorithm/max_element.hpp>

#include <elle/algorithm.hh>
#include <elle/assert.hh>
#include <elle/bytes.hh>
#include <elle/fstream.hh> // rotate_versions.
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
    auto const dumps_path = canonical_folder(xdg_cache_home() / "crashes");
    ELLE_DEBUG("dump to %s", dumps_path);

    auto const log_dir = canonical_folder(xdg_cache_home() / "logs");
    auto const log_base = (log_dir / "main").string();

    // FIXME: Should be unique_ptr, but something in our handling of
    // reactor::Threads prevents it.
    auto res = std::make_shared<CrashReporter>(dumps_path);
    // Attach the logs to the crash dump.
    res->make_payload = [log_dir, log_base] (auto const& base)
      {
        // The greatest NUM in logs/main.<NUM> file names.
        auto const last = [&log_base]() -> boost::optional<int>
        {
          auto const nums = elle::rotate_versions(log_base);
          if (nums.empty())
            return {};
          else
            return *boost::max_element(nums);
        }();
        if (last)
        {
          // The log file next to the minidump file.
          auto const minilog = elle::print("{}.log", base);
          // Get the two last logs in the log directory, if they do
          // match (i.e., don't concatenate main.1 with main.3).
          auto&& o = std::ofstream(minilog);
          for (auto const n: {*last - 1, *last})
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

    // Hook elle::assert to generate the minidumps when an assertion
    // is broken.  We do not want to die immediately though, as we
    // might have some clean up to do.
    elle::on_abort([res](auto const& error) {
        res->write_minidump();
      });
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
