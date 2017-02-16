#pragma once

#include <das/cli.hh>

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
           decltype(modes::mode_create),
           decltype(cli::name),
           decltype(cli::description = boost::none),
           decltype(cli::network),
           decltype(cli::volume),
           decltype(cli::icon = boost::none),
           decltype(cli::push_drive = false),
           decltype(cli::push = false)>
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
           decltype(modes::mode_delete),
           decltype(cli::name),
           decltype(cli::pull = false),
           decltype(cli::purge = false)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/
      Mode<Drive,
           decltype(modes::mode_export),
           decltype(cli::name)>
      export_;
      void
      mode_export(std::string const& name);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      Mode<Drive,
           decltype(modes::mode_fetch),
           decltype(cli::name = boost::none),
           decltype(cli::icon = boost::none)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& name,
                 boost::optional<std::string> const& icon);


      /*---------------.
      | Mode: invite.  |
      `---------------*/
      Mode<Drive,
           decltype(modes::mode_invite),
           decltype(cli::name),
           decltype(cli::user),
           decltype(cli::email),
           decltype(cli::fetch_drive = false),
           decltype(cli::fetch = false),
           decltype(cli::push_invitations = false),
           decltype(cli::push = false),
           decltype(cli::passport = false),
           // FIXME: should be hidden.
           decltype(cli::home = false)>
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
           decltype(modes::mode_join),
           decltype(cli::name)>
      join;
      void
      mode_join(std::string const& name);

      /*-------------.
      | Mode: list.  |
      `-------------*/
      Mode<Drive,
           decltype(modes::mode_list)>
      list;
      void
      mode_list();


      /*-------------.
      | Mode: pull.  |
      `-------------*/
      Mode<Drive,
           decltype(modes::mode_pull),
           decltype(cli::name),
           decltype(cli::purge = false)>
      pull;
      void
      mode_pull(std::string const& name,
                bool purge);


      /*-------------.
      | Mode: push.  |
      `-------------*/
      Mode<Drive,
           decltype(modes::mode_push),
           decltype(cli::name),
           decltype(cli::icon = boost::none)>
      push;
      void
      mode_push(std::string const& name,
                boost::optional<std::string> const& icon);
    };
  }
}
