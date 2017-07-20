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
    {"ASYNC_NOPOP", ""},
    {"ASYNC_POP_DELAY", ""},
    {"ASYNC_SQUASH", ""},
    {"BACKGROUND_DECODE", ""},
    {"BACKTRACE", ""},
    {"BALANCED_TRANSFERS", ""},
    {"BEYOND", ""},
    {"CACHE_REFRESH_BATCH_SIZE", ""},
    {"CONNECT_TIMEOUT", ""},
    {"CRASH", "Generate a crash"},
    {"CRASH_REPORT", "Activate crash-reporting"},
    {"CRASH_REPORT_HOST", ""},
    {"DATA_HOME", ""},
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
    {"SIGNAL_HANDLER", ""},
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
        // Map MEMO_NO_FOO=1 to MEMO_FOO=0, and MEMO_DISABLE_FOO=1 to MEMO_FOO=0.
        for (auto prefix: {"DISABLE_", "NO_"})
          if (auto v2 = elle::tail(*v, prefix))
          {
            const auto var = "MEMO_" + *v2;
            if (elle::os::inenv(var)
                && elle::os::getenv(p.first, false) != !elle::os::getenv(var, true))
            {
              ELLE_WARN("MEMO_%s%s=%s and MEMO_%s=%s conflict:"
                        " proceeding with the latter",
                        prefix, *v, elle::os::getenv(p.first),
                        var, elle::os::getenv(var));
            }
            else
            {
              ELLE_WARN("prefer MEMO_%s=%s to MEMO_%s%s=%s",
                        *v, !elle::os::getenv(p.first, false),
                        prefix, var, elle::os::getenv(p.first, false));
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
