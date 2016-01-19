#include <crash_reporting/CrashReporter.hh>
#include <iostream>

#ifdef INFINIT_LINUX
# include <client/linux/handler/exception_handler.h>
#elif defined(INFINIT_MACOSX)
# include <crash_reporting/gcc_fix.hh>
# include <client/mac/handler/exception_handler.h>
#else
# error Unsupported platform.
#endif

namespace crash_reporting
{
#ifdef INFINIT_LINUX
  static
  bool
  dump_callback(const google_breakpad::MinidumpDescriptor& descriptor,
                void* context,
                bool success)
#elif INFINIT_MACOSX
  static
  bool
  dump_callback(const char* dump_dir,
                const char* minidump_id,
                void* context,
                bool success)
#endif
  {
    return success;
  }

  CrashReporter::CrashReporter(std::string binary_name,
                               std::string dump_path)
    : _binary_name(std::move(binary_name))
    , _dump_path(std::move(dump_path))
    , _exception_handler(nullptr)
  {
#ifdef INFINIT_LINUX
    google_breakpad::MinidumpDescriptor descriptor(this->_dump_path);
    this->_exception_handler =
      new google_breakpad::ExceptionHandler(descriptor,
                                            NULL,
                                            dump_callback,
                                            NULL,
                                            true,
                                            -1);
#elif defined(INFINIT_MACOSX)
    this->_exception_handler =
      new google_breakpad::ExceptionHandler(this->_dump_path,
                                            NULL,
                                            dump_callback,
                                            NULL,
                                            true,
                                            NULL);
#endif
  }

  CrashReporter::~CrashReporter()
  {
    if (this->_exception_handler != nullptr)
      delete this->_exception_handler;
  }

  void
  upload_existing()
  {}
}
