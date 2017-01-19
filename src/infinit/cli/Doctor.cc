#include <infinit/cli/Doctor.hh>

#include <numeric> // iota
#include <regex>

#include <boost/algorithm/string/predicate.hpp>
// FIXME: to remove once migrated to DAS.
#include <boost/program_options.hpp>

#include <elle/bytes.hh>
#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/filesystem/path.hh>
#include <elle/log.hh>
#include <elle/log/TextLogger.hh>
#include <elle/network/Interface.hh>
#include <elle/os/environ.hh>
#include <elle/string/algorithm.hh> // elle::join
#include <elle/system/Process.hh>

#include <cryptography/random.hh>

#include <reactor/connectivity/connectivity.hh>
#include <reactor/filesystem.hh>
#include <reactor/network/upnp.hh>
#include <reactor/scheduler.hh>
#include <reactor/TimeoutGuard.hh>
#include <reactor/http/exceptions.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/GCS.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/Strip.hh>
#include <cryptography/random.hh>
#ifndef INFINIT_WINDOWS
# include <infinit/storage/sftp.hh>
#endif
#include <infinit/storage/S3.hh>


ELLE_LOG_COMPONENT("cli.doctor");

namespace infinit
{
  using Passport = infinit::model::doughnut::Passport;
  namespace cli
  {
    using Error = das::cli::Error;
    namespace bfs = boost::filesystem;
    
#include <infinit/cli/doctor-utility.hh>

    Doctor::Doctor(Infinit& infinit)
      : Entity(infinit)
    {}
  }
}
