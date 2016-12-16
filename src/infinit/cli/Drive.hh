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
    class Drive
      : public Entity<Drive>
    {
    public:
      Drive(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::invite));

      /*---------------.
      | Mode: create.  |
      `---------------*/
      using ModeCreate =
        Mode<decltype(binding(modes::mode_create,
                              cli::name,
                              cli::description = boost::none,
                              cli::network,
                              cli::volume,
                              cli::icon = boost::none,
                              cli::push_drive = false,
                              cli::push = false))>;
      ModeCreate create;
      void
      mode_create(std::string const& name,
                  boost::optional<std::string> const& description,
                  std::string const& network,
                  std::string const& volume,
                  boost::optional<std::string> const& icon,
                  bool push_drive,
                  bool push);

      /*---------------.
      | Mode: invite.  |
      `---------------*/
      using ModeInvite =
        Mode<decltype(binding(modes::mode_invite,
                              cli::name,
                              cli::user,
                              cli::email,
                              cli::fetch_drive = false,
                              cli::fetch = false,
                              cli::push_invitations = false,
                              cli::push = false,
                              cli::passport = false,
                              // FIXME: should be hidden.
                              cli::permissions = boost::none,
                              cli::home = false))>;
      ModeInvite invite;
      void
      mode_invite(std::string const& name,
                  std::vector<std::string> const& user,
                  std::vector<std::string> const& email,
                  bool fetch_drive,
                  bool fetch,
                  bool push_invitations,
                  bool push,
                  bool passport,
                  boost::optional<std::string> const& permissions = {},
                  bool home = false);
    };
  }
}
