#pragma once

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/model/doughnut/Passport.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Drive
      : public Entity<Drive>
    {
    public:
      using Passport = infinit::model::doughnut::Passport;

      Drive(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::invite,
                                    cli::join,
                                    cli::list,
                                    cli::pull,
                                    cli::push));

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
      | Mode: delete.  |
      `---------------*/
      using ModeDelete =
        Mode<decltype(binding(modes::mode_delete,
                              cli::name,
                              cli::pull = false,
                              cli::purge = false))>;
      ModeDelete delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/
      using ModeExport =
        Mode<decltype(binding(modes::mode_export,
                              cli::name))>;
      ModeExport export_;
      void
      mode_export(std::string const& name);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      using ModeFetch =
        Mode<decltype(binding(modes::mode_fetch,
                              cli::name = boost::none,
                              cli::icon = boost::none))>;
      ModeFetch fetch;
      void
      mode_fetch(boost::optional<std::string> const& name,
                 boost::optional<std::string> const& icon);


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
                  bool home = false);


      /*-------------.
      | Mode: join.  |
      `-------------*/
      using ModeJoin =
        Mode<decltype(binding(modes::mode_join,
                              cli::name))>;
      ModeJoin join;
      void
      mode_join(std::string const& name);


      /*-------------.
      | Mode: list.  |
      `-------------*/
      using ModeList =
        Mode<decltype(binding(modes::mode_list))>;
      ModeList list;
      void
      mode_list();



      /*-------------.
      | Mode: pull.  |
      `-------------*/
      using ModePull =
        Mode<decltype(binding(modes::mode_pull,
                              cli::name,
                              cli::purge = false))>;
      ModePull pull;
      void
      mode_pull(std::string const& name,
                bool purge);


      /*-------------.
      | Mode: push.  |
      `-------------*/
      using ModePush =
        Mode<decltype(binding(modes::mode_push,
                              cli::name))>;
      ModePush push;
      void
      mode_push(std::string const& name);
    };
  }
}
