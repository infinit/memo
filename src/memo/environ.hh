#pragma once

#include <elle/assert.hh>
#include <elle/os/environ.hh>
#include <elle/print.hh>

namespace memo
{
  /// Make sure the environment variables make sense.
  ///
  /// Memoed, actually runs only once.
  void
  environ_check();

  /// Whether this is a known variable suffix (e.g., "HOME", not "MEMO_HOME").
  bool
  environ_valid_name(std::string const& v);

  /// Get the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  template <typename T>
  T
  getenv(std::string const& v, T const& def)
  {
    ELLE_ASSERT(environ_valid_name(v));
    return elle::os::getenv("MEMO_" + v, def);
  }

  /// Set the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  template <typename T>
  void
  setenv(std::string const& v, T const& val)
  {
    ELLE_ASSERT(environ_valid_name(v));
    elle::os::setenv("MEMO_" + v, elle::print("%s", val));
  }

  /// Unset the value of this Memo variable.
  ///
  /// The name will be prefixed with MEMO_.
  inline
  void
  unsetenv(std::string const& v)
  {
    ELLE_ASSERT(environ_valid_name(v));
    return elle::os::unsetenv("MEMO_" + v);
  }
}
