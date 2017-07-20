#pragma once

#include <elle/os/environ.hh>
#include <elle/print.hh>

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

  /// Set the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  template <typename T>
  void
  setenv(std::string const& v, T const& val)
  {
    check_environment();
    elle::os::setenv("MEMO_" + v, elle::print("%s", val));
  }

  /// Unset the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  inline
  void
  unsetenv(std::string const& v)
  {
    check_environment();
    return elle::os::unsetenv("MEMO_" + v);
  }
}
