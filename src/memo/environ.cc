#include <memo/environ.hh>

#include <map>

#include <elle/algorithm.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh>

ELLE_LOG_COMPONENT("memo.environ");

namespace
{
  auto const vars = std::map<std::string, std::string>
  {
    {"ASYNC_DISABLE_SQUASH", ""},
    {"ASYNC_NOPOP", ""},
    {"ASYNC_POP_DELAY", ""},
    {"BACKTRACE", ""},
    {"BEYOND", ""},
    {"CACHE_REFRESH_BATCH_SIZE", ""},
    {"CONNECT_TIMEOUT", ""},
    {"CRASH", "Generate a crash"},
    {"CRASH_REPORT", "Activate crash-reporting"},
    {"CRASH_REPORT_HOST", ""},
    {"DATA_HOME", ""},
    {"DISABLE_BALANCED_TRANSFERS", ""},
    {"DISABLE_SIGNAL_HANDLER", ""},
    {"FIRST_BLOCK_DATA_SIZE", ""},
    {"HOME", ""},
    {"HOME_OVERRIDE", ""},
    {"IPV4", "Enable IPv4 (default: true)"},
    {"IPV6", "Enable IPv6 (default: true)"},
    {"KELIPS_ASYNC", ""},
    {"KELIPS_ASYNC_SEND", ""},
    {"KELIPS_NO_SNUB", ""},
    {"LOG_REACHABILITY", ""},
    {"LOOKAHEAD_BLOCKS", ""},
    {"LOOKAHEAD_THREADS", ""},
    {"MAX_EMBED_SIZE", ""},
    {"MAX_SQUASH_SIZE", ""},
    {"NO_BACKGROUND_DECODE", ""},
    {"NO_IPV4", "Disable IPv4"},
    {"NO_IPV6", "Disable IPv6"},
    {"NO_PREEMPT_DECODE", ""},
    {"PAXOS_LENIENT_FETCH", ""},
    {"PREFETCH_DEPTH", ""},
    {"PREFETCH_GROUP", ""},
    {"PREFETCH_TASKS", ""},
    {"PREFETCH_THREADS", ""},
    {"PRESERVE_ACLS", ""},
    {"PROMETHEUS_ENDPOINT", ""},
    {"RDV", ""},
    {"RPC_DISABLE_CRYPTO", ""},
    {"RPC_SERVE_THREADS", ""},
    {"SOFTFAIL_RUNNING", ""},
    {"SOFTFAIL_TIMEOUT", ""},
    {"USER", ""},
    {"UTP", ""},
  };

  bool
  check_environment_()
  {
    auto warn = false;
    auto const env = elle::os::environ();
    ELLE_DUMP("checking: %s", env);
    for (auto const& p: env)
      if (auto v = elle::tail(p.first, "MEMO_"))
      {
        if (!elle::contains(vars, *v))
        {
          ELLE_WARN("suspicious environment variable: MEMO_%s", *v);
          warn = true;
        }
        if (auto v2 = elle::tail(*v, "NO_"))
        {
          const auto var = "MEMO_" + *v2;
          // MEMO_NO_FOO=b stands for MEMO_FOO=!b.
          if (elle::os::inenv(var)
              && elle::os::getenv(p.first, false) != !elle::os::getenv(var, true))
          {
            ELLE_WARN("MEMO_NO_%s=%s and MEMO_%s=%s conflict:"
                      " proceeding with the latter",
                      *v, elle::os::getenv(p.first),
                      var, elle::os::getenv(var));
          }
          else
          {
            ELLE_WARN("prefer MEMO_%s=%s to MEMO_NO_%s=%s",
                      *v, !elle::os::getenv(p.first, false),
                      var, elle::os::getenv(p.first, false));
            elle::os::setenv(var,
                             elle::print("%s", !elle::os::getenv(p.first, false)));
          }
        }
      }
    if (warn)
      ELLE_WARN("known MEMO_* environment variables: %s", elle::keys(vars));
    return true;
  }
}

namespace memo
{
  void
  check_environment()
  {
    ELLE_COMPILER_ATTRIBUTE_MAYBE_UNUSED
    static auto _ = check_environment_();
  }
}
