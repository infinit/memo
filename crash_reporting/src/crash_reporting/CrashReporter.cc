#include <crash_reporting/CrashReporter.hh>
#include <iostream>

#include <Availability.h>
#ifdef __OSX_AVAILABLE_STARTING
# undef __OSX_AVAILABLE_STARTING
# define __OSX_AVAILABLE_STARTING(_osx, _ios)
#endif
#include <client/mac/handler/exception_handler.h>

namespace crash_reporting
{
  static
  bool
  dump_callback(const char* dump_dir,
                const char* minidump_id,
                void* context,
                bool success)
  {
    std::cout << "saved dump to " << dump_dir << "/" << minidump_id
              << std::endl;
    return success;
  }

  CrashReporter::CrashReporter(std::string binary_name,
                               std::string dump_path)
    : _binary_name(std::move(binary_name))
    , _dump_path(std::move(dump_path))
    , _exception_handler(nullptr)
  {
    this->_exception_handler =
      new google_breakpad::ExceptionHandler(this->_dump_path,
                                            NULL,
                                            dump_callback,
                                            NULL,
                                            true,
                                            NULL);
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
