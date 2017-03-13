#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
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
      : public Object<Drive>
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
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::volume)::Formal<std::string const&>,
                 decltype(cli::icon = boost::optional<std::string>()),
                 decltype(cli::push_drive = false),
                 decltype(cli::push = false)),
           decltype(modes::mode_create)>
      create;
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
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::pull = false),
                 decltype(cli::purge = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& name);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      Mode<Drive,
           void (decltype(cli::name = boost::optional<std::string>()),
                 decltype(cli::icon = boost::optional<std::string>())),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& name,
                 boost::optional<std::string> const& icon);

      /*---------------.
      | Mode: invite.  |
      `---------------*/
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::vector<std::string> const&>,
                 decltype(cli::email)::Formal<std::vector<std::string> const&>,
                 decltype(cli::fetch_drive = false),
                 decltype(cli::fetch = false),
                 decltype(cli::push_invitations = false),
                 decltype(cli::push = false),
                 decltype(cli::passport = false),
                 // FIXME: should be hidden.
                 decltype(cli::home = false)),
           decltype(modes::mode_invite)>
      invite;
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
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_join)>
      join;
      void
      mode_join(std::string const& name);

      /*-------------.
      | Mode: list.  |
      `-------------*/
      Mode<Drive,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();


      /*-------------.
      | Mode: pull.  |
      `-------------*/
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::purge = false)),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(std::string const& name,
                bool purge);


      /*-------------.
      | Mode: push.  |
      `-------------*/
      Mode<Drive,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::icon = boost::optional<std::string>())),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& name,
                boost::optional<std::string> const& icon);
    };
  }
}
