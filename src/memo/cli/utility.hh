#pragma once

#include <string>

#include <boost/filesystem/path.hpp>

#include <elle/unordered_map.hh>
#include <elle/das/cli.hh>

#include <elle/reactor/network/upnp.hh>

#include <memo/Network.hh>
#include <memo/model/doughnut/Doughnut.hh>

namespace memo
{
  namespace cli
  {
    namespace bfs = boost::filesystem;
    namespace dnut = memo::model::doughnut;

    struct VarMap
    {
      /// Variable name -> value.
      using Map = elle::unordered_map<std::string, std::string>;

      template <typename... Args>
      VarMap(Args&&... args)
        : vars(std::forward<Args>(args)...)
      {}

      VarMap(std::initializer_list<Map::value_type> l)
        : vars(std::move(l))
      {}

      /// Perform metavariable substitution.
      std::string
      expand(std::string const& s) const;

      Map vars;
    };

    struct Endpoints
    {
      std::vector<std::string> addresses;
      int port;
      using Model = elle::das::Model<
        Endpoints,
        decltype(elle::meta::list(memo::symbols::addresses,
                                  memo::symbols::port))>;
    };

    template <typename T>
    T const&
    mandatory(boost::optional<T> const& opt, std::string const& name)
    {
      if (opt)
        return *opt;
      else
        throw elle::das::cli::MissingOption(name);
    }
  }
}

ELLE_DAS_SERIALIZE(memo::cli::Endpoints);

namespace memo
{
  namespace cli
  {
    class InterfacePublisher
    {
    public:
      friend class elle::With<InterfacePublisher>;
    private:
      InterfacePublisher(memo::Memo const& memo,
                         memo::Network const& network,
                         memo::User const& self,
                         memo::model::Address const& node_id,
                         int port,
                         boost::optional<std::vector<std::string>> advertise = {},
                         bool no_local_endpoints = false,
                         bool no_public_endpoints = false);

      ~InterfacePublisher();

      ELLE_ATTRIBUTE(memo::Memo const&, memo);
      ELLE_ATTRIBUTE(std::string, url);
      ELLE_ATTRIBUTE(memo::Network const&, network);
      ELLE_ATTRIBUTE(memo::User, self);
      ELLE_ATTRIBUTE(std::shared_ptr<elle::reactor::network::UPNP>, upnp);
      ELLE_ATTRIBUTE(elle::reactor::network::PortMapping, port_map_tcp);
      ELLE_ATTRIBUTE(elle::reactor::network::PortMapping, port_map_udp);
    };

    std::unique_ptr<std::istream>
    commands_input(boost::optional<std::string> input_name);

    /*---------.
    | Daemon.  |
    `---------*/

    bfs::path
    daemon_sock_path();

#ifndef MEMO_WINDOWS
    using DaemonHandle = int;
    constexpr auto daemon_invalid = -1;

    DaemonHandle
    daemon_hold(int nochdir, int noclose);

    void
    daemon_release(DaemonHandle handle);
#endif

    /// Hook USR1 and display some statistics
    void
    hook_stats_signals(memo::model::doughnut::Doughnut& dht);

    model::NodeLocations
    hook_peer_discovery(model::doughnut::Doughnut& model, std::string file);

    void
    port_to_file(uint16_t port,
                 bfs::path const& path_);

    void
    endpoints_to_file(memo::model::Endpoints endpoints,
                      bfs::path const& path_);


    /// Turn a list of addresses (e.g., `foo.bar.fr:http`) and/or
    /// filenames that contains such addresses, into a list of
    /// Endpoints.
    ///
    /// Yes, a list of Endpoints, not a list of Endpoint, because
    /// foo.bar.fr might actually denote several hosts, and we want
    /// to reach each one individually.
    std::vector<memo::model::Endpoints>
    parse_peers(std::vector<std::string> const& peers);

    void
    ensure_version_is_supported(elle::Version const& version);

    /// Recognize "utp", "tcp", or "all".  Default is "all".
    dnut::Protocol
    protocol_get(boost::optional<std::string> const& proto);

    /// Map "(r|w|rw)" to "set$1", and "" to "".
    std::string
    mode_get(boost::optional<std::string> const& mode);
  }
}
