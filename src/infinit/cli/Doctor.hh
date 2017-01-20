#pragma once

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Doctor
      : public Entity<Doctor>
    {
    public:
      /// Default server to ping with.
      static std::string const connectivity_server;

      Doctor(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::all,
                                    cli::configuration,
                                    cli::connectivity,
                                    cli::networking,
                                    cli::system));

      /*------------.
      | Mode: all.  |
      `------------*/
      using ModeAll =
        Mode<decltype(binding(modes::mode_all,
                              cli::ignore_non_linked = false,
                              cli::upnp_tcp_port = boost::none,
                              cli::upnp_udt_port = boost::none,
                              cli::server = connectivity_server,
                              cli::verbose = false))>;
      ModeAll all;
      void
      mode_all(bool ignore_non_linked,
               boost::optional<uint16_t> upnp_tcp_port,
               boost::optional<uint16_t> upnp_udt_port,
               boost::optional<std::string> const& server,
               bool verbose);

      /*----------------------.
      | Mode: configuration.  |
      `----------------------*/
      using ModeConfiguration =
        Mode<decltype(binding(modes::mode_configuration,
                              cli::ignore_non_linked = false,
                              cli::verbose = false))>;
      ModeConfiguration configuration;
      void
      mode_configuration(bool ignore_non_linked,
                         bool verbose);

      /*---------------------.
      | Mode: connectivity.  |
      `---------------------*/
      using ModeConnectivity =
        Mode<decltype(binding(modes::mode_connectivity,
                              cli::upnp_tcp_port = boost::none,
                              cli::upnp_udt_port = boost::none,
                              cli::server = connectivity_server,
                              cli::verbose = false))>;
      ModeConnectivity connectivity;
      void
      mode_connectivity(boost::optional<uint16_t> upnp_tcp_port,
                        boost::optional<uint16_t> upnp_udt_port,
                        boost::optional<std::string> const& server,
                        bool verbose);

      /*-------------------.
      | Mode: networking.  |
      `-------------------*/
      using ModeNetworking =
        Mode<decltype(binding(modes::mode_networking,
                              cli::mode = boost::none,
                              cli::protocol = boost::none,
                              cli::packet_size = boost::none,
                              cli::packets_count = boost::none,
                              cli::host = boost::none,
                              cli::port = boost::none,
                              cli::tcp_port = boost::none,
                              cli::utp_port = boost::none,
                              cli::xored_utp_port = boost::none,
                              cli::xored = boost::none,
                              cli::verbose = false))>;
      ModeNetworking networking;
      void
      mode_networking(boost::optional<std::string> const& mode_name,
                      boost::optional<std::string> const& protocol_name,
                      boost::optional<elle::Buffer::Size> packet_size,
                      boost::optional<int64_t> packets_count,
                      boost::optional<std::string> const& host,
                      boost::optional<uint16_t> port,
                      boost::optional<uint16_t> tcp_port,
                      boost::optional<uint16_t> utp_port,
                      boost::optional<uint16_t> xored_utp_port,
                      boost::optional<std::string> const& xored,
                      bool verbose);

      /*---------------.
      | Mode: system.  |
      `---------------*/
      using ModeSystem =
        Mode<decltype(binding(modes::mode_system,
                              cli::verbose = false))>;
      ModeSystem system;
      void
      mode_system(bool verbose);
    };
  }
}
