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
                                    cli::fetch,
                                    cli::import,
                                    cli::link,
                                    cli::list,
                                    cli::unlink,
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
                  boost::optional<std::string> const& description = {},
                  Strings const& storage = {},
                  boost::optional<int> port = boost::none,
                  int replication_factor = 1,
                  boost::optional<std::string> const& eviction_delay = boost::none,
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
                  boost::optional<std::string> const& kelips_contact_timeout = boost::none,
                  boost::optional<std::string> const& encrypt = boost::none,
                  boost::optional<std::string> const& protocol = boost::none);


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

      /*--------------.
      | Mode: fetch.  |
      `--------------*/

      using ModeFetch =
        Mode<decltype(binding(modes::mode_fetch,
                              cli::name = boost::none))>;
      ModeFetch fetch;
      void
      mode_fetch(boost::optional<std::string> const& network_name = {});


      /*---------------.
      | Mode: import.  |
      `---------------*/
      using ModeImport =
        Mode<decltype(binding(modes::mode_import,
                              cli::input = boost::none))>;
      ModeImport import;
      void
      mode_import(boost::optional<std::string> const& input_name = {});


      /*-------------.
      | Mode: link.  |
      `-------------*/
      using ModeLink =
        Mode<decltype(binding(modes::mode_link,
                              cli::name,
                              cli::storage = Strings{},
                              cli::output = boost::none))>;
      ModeLink link;
      void
      mode_link(std::string const& network_name,
                Strings const& storage_names = {},
                boost::optional<std::string> const& output_name = {});


      /*-------------.
      | Mode: list.  |
      `-------------*/
      using ModeList =
        Mode<decltype(binding(modes::mode_list))>;
      ModeList list;
      void
      mode_list();


      /*---------------.
      | Mode: unlink.  |
      `---------------*/
      using ModeUnlink =
        Mode<decltype(binding(modes::mode_unlink,
                              cli::name))>;
      ModeUnlink unlink;
      void
      mode_unlink(std::string const& network_name);


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
                  boost::optional<std::string> const& description = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> const& output_name = {},
                  bool push_network = false,
                  bool push = false,
                  Strings const& admin_r = Strings{},
                  Strings const& admin_rw = Strings{},
                  Strings const& admin_remove = Strings{},
                  boost::optional<std::string> const& mountpoint = {},
                  Strings const& peer = Strings{});
    };
  }
}
