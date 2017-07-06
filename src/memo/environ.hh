#pragma once

#include <elle/os/getenv.hh>

namespace memo
{
  /// Make sure the environment variables make sense.
  void
  check_environment();

  /// Get the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  template <typename T>
  T
  getenv(std::string v, T const& def)
  {
    return elle::os::getenv("MEMO_" + v, def);
  }
}
