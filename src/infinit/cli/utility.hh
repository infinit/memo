#pragma once

#include <string>

#include <boost/filesystem/path.hpp>

#include <elle/unordered_map.hh>
#include <das/cli.hh>

#include <reactor/network/upnp.hh>

#include <infinit/Network.hh>
#include <infinit/model/doughnut/Doughnut.hh>

namespace infinit
{
  namespace cli
  {
    std::unique_ptr<std::istream>
    commands_input(boost::optional<std::string> input_name);

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
      using Model = das::Model<
        Endpoints,
        decltype(elle::meta::list(infinit::symbols::addresses,
                                  infinit::symbols::port))>;
    };

    template <typename T>
    T const&
    mandatory(boost::optional<T> const& opt, std::string const& name)
    {
      if (opt)
        return *opt;
      else
        throw das::cli::MissingOption(name);
    }
  }
}

DAS_SERIALIZE(infinit::cli::Endpoints);

namespace infinit
{
  namespace cli
  {
    class InterfacePublisher
    {
    public:
      friend class elle::With<InterfacePublisher>;
    private:
      InterfacePublisher(infinit::Network const& network,
                         infinit::User const& self,
                         infinit::model::Address const& node_id,
                         int port,
                         boost::optional<std::vector<std::string>> advertise = {},
                         bool no_local_endpoints = false,
                         bool no_public_endpoints = false);

      ~InterfacePublisher();

      ELLE_ATTRIBUTE(std::string, url);
      ELLE_ATTRIBUTE(infinit::Network const&, network);
      ELLE_ATTRIBUTE(infinit::User, self);
      ELLE_ATTRIBUTE(std::shared_ptr<reactor::network::UPNP>, upnp);
      ELLE_ATTRIBUTE(reactor::network::PortMapping, port_map_tcp);
      ELLE_ATTRIBUTE(reactor::network::PortMapping, port_map_udp);
    };

    /*---------.
    | Daemon.  |
    `---------*/

    namespace bfs = boost::filesystem;

    bfs::path
    daemon_sock_path();

#ifndef INFINIT_WINDOWS
    using DaemonHandle = int;
    constexpr auto daemon_invalid = -1;

    DaemonHandle
    daemon_hold(int nochdir, int noclose);

    void
    daemon_release(DaemonHandle handle);
#endif

    /// Hook USR1 and display some statistics
    void
    hook_stats_signals(infinit::model::doughnut::Doughnut& dht);

    model::NodeLocations
    hook_peer_discovery(model::doughnut::Doughnut& model, std::string file);

    void
    port_to_file(uint16_t port,
                 boost::filesystem::path const& path_);

    void
    endpoints_to_file(infinit::model::Endpoints endpoints,
                      boost::filesystem::path const& path_);
  }
}
