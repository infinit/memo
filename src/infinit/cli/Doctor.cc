#include <infinit/cli/Doctor.hh>

#include <numeric> // iota
#include <regex>

#include <boost/algorithm/cxx11/all_of.hpp>
#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/algorithm/find_if.hpp>

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
      , connectivity(
        "Perform connectivity checks",
        das::cli::Options(),
        this->bind(modes::mode_connectivity,
                   cli::upnp_tcp_port = boost::none,
                   cli::upnp_udt_port = boost::none,
                   cli::server = std::string{"connectivity.infinit.sh"},
                   cli::verbose = false))
      , system(
        "Perform sanity checks on your system",
        das::cli::Options(),
        this->bind(modes::mode_system,
                   cli::verbose = false))
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



    /*---------------------.
    | Mode: connectivity.  |
    `---------------------*/

    void
    Doctor::mode_connectivity(boost::optional<uint16_t> upnp_tcp_port,
                              boost::optional<uint16_t> upnp_udt_port,
                              boost::optional<std::string> const& server,
                              bool verbose)
    {
      ELLE_TRACE_SCOPE("connectivity");
      auto& cli = this->cli();

      auto results = ConnectivityResults{};
      _connectivity(cli,
                    server,
                    upnp_tcp_port,
                    upnp_udt_port,
                    results);
      _output(cli, std::cout, results, verbose);
      _report_error(cli, std::cout, results.sane(), results.warning());
    }


    /*---------------.
    | Mode: system.  |
    `---------------*/

    void
    Doctor::mode_system(bool verbose)
    {
      ELLE_TRACE_SCOPE("system");
      auto& cli = this->cli();

      auto results = SystemSanityResults{};
      _system_sanity(cli, results);
      _output(cli, std::cout, results, verbose);
      _report_error(cli, std::cout, results.sane(), results.warning());
    }
  }
}
