#pragma once

namespace memo
{
  /// Set up the "critical" log, i.e., the log family typically stored
  /// in `~/.cache/infinit/logs/main.*`.
  ///
  /// Reads `$MEMO_LOG_LEVEL`.
  void make_critical_log();
}
