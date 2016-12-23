#pragma once

#include <string>

#include <boost/filesystem/path.hpp>

#include <elle/unordered_map.hh>

#include <infinit/model/doughnut/Doughnut.hh>

namespace infinit
{
  namespace cli
  {
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
  }
}
