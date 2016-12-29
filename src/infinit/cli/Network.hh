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
    class Network
      : public Entity<Network>
    {
    public:
      Network(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::export_,
                                    cli::update));

      using Strings = std::vector<std::string>;

      /*---------------.
      | Mode: create.  |
      `---------------*/
      using ModeCreate =
        Mode<decltype(binding(modes::mode_create,
                              cli::name,
                              cli::description = boost::none,
                              cli::storage = Strings{},
                              cli::port = boost::none,
                              cli::replication_factor = 1,
                              cli::eviction_delay = boost::none,
                              cli::output = boost::none,
                              cli::push_network = false,
                              cli::push = false,
                              cli::admin_r = Strings{},
                              cli::admin_rw = Strings{},
                              cli::peer = Strings{},
                              // Consensus types.
                              cli::paxos = false,
                              cli::no_consensus = false,
                              // Overlay types.
                              cli::kelips = false,
                              cli::kalimero = false,
                              cli::kouncil = false,
                              // Kelips options,
                              cli::nodes = boost::none,
                              cli::k = boost::none,
                              cli::kelips_contact_timeout = boost::none,
                              cli::encrypt = boost::none,
                              cli::protocol = boost::none))>;
      ModeCreate create;
      void
      mode_create(std::string const& network_name,
                  boost::optional<std::string> description = {},
                  Strings const& storage = {},
                  boost::optional<int> port = boost::none,
                  int replication_factor = 1,
                  boost::optional<std::string> eviction_delay = boost::none,
                  boost::optional<std::string> const& output_name = boost::none,
                  bool push_network = false,
                  bool push = false,
                  Strings const& admin_r = {},
                  Strings const& admin_rw = {},
                  Strings const& peer = {},
                  // Consensus types.
                  bool paxos = false,
                  bool no_consensus = false,
                  // Overlay types.
                  bool kelips = false,
                  bool kalimero = false,
                  bool kouncil = false,
                  // Kelips options,
                  boost::optional<int> nodes = boost::none,
                  boost::optional<int> k = boost::none,
                  boost::optional<std::string> kelips_contact_timeout = boost::none,
                  boost::optional<std::string> encrypt = boost::none,
                  boost::optional<std::string> protocol = boost::none);


      /*---------------.
      | Mode: export.  |
      `---------------*/
      using ModeExport =
        Mode<decltype(binding(modes::mode_export,
                              cli::name,
                              cli::output = boost::none))>;
      ModeExport export_;
      void
      mode_export(std::string const& volume_name,
                  boost::optional<std::string> const& output_name = {});

      /*---------------.
      | Mode: update.  |
      `---------------*/

      using ModeUpdate =
        Mode<decltype(binding(modes::mode_update,
                              cli::name,
                              cli::description = boost::none,
                              cli::port = boost::none,
                              cli::output = boost::none,
                              cli::push_network = false,
                              cli::push = false,
                              cli::admin_r = Strings{},
                              cli::admin_rw = Strings{},
                              cli::admin_remove = Strings{},
                              cli::mountpoint = boost::none,
                              cli::peer = Strings{}))>;
      ModeUpdate update;
      void
      mode_update(std::string const& network_name,
                  boost::optional<std::string> description = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> const& output_name = {},
                  bool push_network = false,
                  bool push = false,
                  std::vector<std::string> const& admin_r = Strings{},
                  std::vector<std::string> const& admin_rw = Strings{},
                  std::vector<std::string> const& admin_remove = Strings{},
                  boost::optional<std::string> mountpoint = {},
                  std::vector<std::string> const& peer = Strings{});
    };
  }
}
