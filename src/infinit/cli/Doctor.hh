#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Doctor
      : public Object<Doctor>
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
      Mode<Doctor,
           decltype(modes::mode_all),
           decltype(cli::ignore_non_linked = false),
           decltype(cli::upnp_tcp_port = boost::none),
           decltype(cli::upnp_udt_port = boost::none),
           decltype(cli::server = connectivity_server),
           decltype(cli::no_color = false),
           decltype(cli::verbose = false)>
      all;
      void
      mode_all(bool ignore_non_linked,
               boost::optional<uint16_t> upnp_tcp_port,
               boost::optional<uint16_t> upnp_udt_port,
               boost::optional<std::string> const& server,
               bool no_color,
               bool verbose);

      /*----------------------.
      | Mode: configuration.  |
      `----------------------*/
      Mode<Doctor,
           decltype(modes::mode_configuration),
           decltype(cli::ignore_non_linked = false),
           decltype(cli::no_color = false),
           decltype(cli::verbose = false)>
      configuration;
      void
      mode_configuration(bool ignore_non_linked,
                         bool no_color,
                         bool verbose);

      /*---------------------.
      | Mode: connectivity.  |
      `---------------------*/
      Mode<Doctor,
           decltype(modes::mode_connectivity),
           decltype(cli::upnp_tcp_port = boost::none),
           decltype(cli::upnp_udt_port = boost::none),
           decltype(cli::server = connectivity_server),
           decltype(cli::no_color = false),
           decltype(cli::verbose = false)>
      connectivity;
      void
      mode_connectivity(boost::optional<uint16_t> upnp_tcp_port,
                        boost::optional<uint16_t> upnp_udt_port,
                        boost::optional<std::string> const& server,
                        bool no_color,
                        bool verbose);

      /*-------------------.
      | Mode: networking.  |
      `-------------------*/
      Mode<Doctor,
           decltype(modes::mode_networking),
           decltype(cli::mode = boost::none),
           decltype(cli::protocol = boost::none),
           decltype(cli::packet_size = boost::none),
           decltype(cli::packets_count = boost::none),
           decltype(cli::host = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::tcp_port = boost::none),
           decltype(cli::utp_port = boost::none),
           decltype(cli::xored_utp_port = boost::none),
           decltype(cli::xored = boost::none),
           decltype(cli::no_color = false),
           decltype(cli::verbose = false)>
      networking;
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
                      bool no_color,
                      bool verbose);

      /*---------------.
      | Mode: system.  |
      `---------------*/
      Mode<Doctor,
           decltype(modes::mode_system),
           decltype(cli::no_color = false),
           decltype(cli::verbose = false)>
      system;
      void
      mode_system(bool no_color,
                  bool verbose);
    };
  }
}
