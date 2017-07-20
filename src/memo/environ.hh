#pragma once

#include <elle/os/environ.hh>

namespace memo
{
  /// Make sure the environment variables make sense.
  ///
  /// Memoed, actually runs only once.
  void
  check_environment();

  /// Get the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  template <typename T>
  T
  getenv(std::string const& v, T const& def)
  {
    check_environment();
    return elle::os::getenv("MEMO_" + v, def);
  }
}
