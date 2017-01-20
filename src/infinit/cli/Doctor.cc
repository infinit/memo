#include <infinit/cli/Doctor.hh>

#include <numeric> // iota
#include <regex>

#include <boost/algorithm/string/predicate.hpp>

#include <elle/bytes.hh>
#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/filesystem/path.hh>
#include <elle/log.hh>
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
      , configuration(
        "Perform integrity checks on the Infinit configuration files",
        das::cli::Options(),
        this->bind(modes::mode_configuration,
                   cli::verbose = false,
                   cli::ignore_non_linked = false))
    {}


    /*----------------------.
    | Mode: configuration.  |
    `----------------------*/

    void
    Doctor::mode_configuration(bool verbose,
                               bool ignore_non_linked)
    {
      ELLE_TRACE_SCOPE("configuration");
      auto& cli = this->cli();

      auto results = ConfigurationIntegrityResults{};
      _configuration_integrity(cli, ignore_non_linked, results);
      _output(cli, std::cout, results, verbose);
      _report_error(cli, std::cout, results.sane(), results.warning());
    }
  }
}
