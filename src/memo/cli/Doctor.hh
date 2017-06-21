#pragma once

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Doctor
      : public Object<Doctor>
    {
    public:
      /// Default server to ping with.
      static std::string const connectivity_server;

      Doctor(Memo& memo);
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
           void (decltype(cli::ignore_non_linked = false),
                 decltype(cli::upnp_tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::upnp_udt_port = boost::optional<uint16_t>()),
                 decltype(cli::server = connectivity_server),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_all)>
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
           void (decltype(cli::ignore_non_linked = false),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_configuration)>
      configuration;
      void
      mode_configuration(bool ignore_non_linked,
                         bool no_color,
                         bool verbose);

      /*---------------------.
      | Mode: connectivity.  |
      `---------------------*/
      Mode<Doctor,
           void (decltype(cli::upnp_tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::upnp_udt_port = boost::optional<uint16_t>()),
                 decltype(cli::server = connectivity_server),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_connectivity)>
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
           void (decltype(cli::mode = boost::optional<std::string>()),
                 decltype(cli::protocol = boost::optional<std::string>()),
                 decltype(cli::packet_size =
                          boost::optional<elle::Buffer::Size>()),
                 decltype(cli::packets_count = boost::optional<int64_t>()),
                 decltype(cli::host = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<uint16_t>()),
                 decltype(cli::tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::utp_port = boost::optional<uint16_t>()),
                 decltype(cli::xored_utp_port = boost::optional<uint16_t>()),
                 decltype(cli::xored = boost::optional<std::string>("both")),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_networking)>
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
                      boost::optional<std::string> const& xored = std::string{"both"},
                      bool no_color = false,
                      bool verbose = false);

      /*---------------.
      | Mode: system.  |
      `---------------*/
      Mode<Doctor,
           void (decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_system)>
      system;
      void
      mode_system(bool no_color,
                  bool verbose);
    };
  }
}
