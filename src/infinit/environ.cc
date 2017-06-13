#include <infinit/environ.hh>

#include <map>

#include <elle/algorithm.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh>

ELLE_LOG_COMPONENT("infinit.environ");

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
    {"CRASH_REPORT", "Activate crash-reporting (new name)"},
    {"CRASH_REPORTER", "Activate crash-reporting (old name)"},
    {"CRASH_REPORT_HOST", ""},
    {"DATA_HOME", ""},
    {"DISABLE_BALANCED_TRANSFERS", ""},
    {"DISABLE_SIGNAL_HANDLER", ""},
    {"FIRST_BLOCK_DATA_SIZE", ""},
    {"HOME", ""},
    {"HOME_OVERRIDE", ""},
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
}  

namespace infinit
{
  void
  check_environment()
  {
    auto warn = false;
    ELLE_DUMP("checking: %s", elle::os::environ());
    for (auto const& p: elle::os::environ())
      if (auto v = elle::tail(p.first, "INFINIT_"))
        if (!elle::contains(vars, *v))
        {
          ELLE_WARN("suspicious environment variable: INFINIT_%s", *v);
          warn = true;
        }
    if (warn)
      ELLE_WARN("known INFINIT_* environment variables: %s", elle::keys(vars));
  }
}
