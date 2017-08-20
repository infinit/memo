#pragma once

#include <memory>

#include <elle/reactor/Thread.hh>

#include <boost/filesystem/path.hpp>

namespace memo
{
  /// Thread to send the crash reports (some might be pending from
  /// previous runs, saved on disk).
  ///
  /// Possibly null, for instance on unsupported platforms.
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread();
}
