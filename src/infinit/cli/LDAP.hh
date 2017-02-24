#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class LDAP
      : public Object<LDAP>
    {
    public:
      LDAP(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::drive_invite,
                                    cli::populate_hub,
                                    cli::populate_network));

      /*---------------------.
      | Mode: drive invite.  |
      `---------------------*/
      Mode<LDAP,
           void (decltype(cli::server)::Formal<std::string const&>,
                 decltype(cli::domain)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::drive)::Formal<std::string const&>,
                 decltype(cli::root_permissions = std::string()),
                 decltype(cli::create_home = false),
                 decltype(cli::searchbase)::Formal<std::string const&>,
                 decltype(cli::filter = boost::optional<std::string>()),
                 decltype(cli::object_class = boost::optional<std::string>()),
                 decltype(cli::mountpoint)::Formal<std::string const&>,
                 decltype(cli::deny_write = false),
                 decltype(cli::deny_storage = false)),
           decltype(modes::mode_drive_invite)>
      drive_invite;
      void
      mode_drive_invite(std::string const& server,
                        std::string const& domain,
                        std::string const& user,
                        boost::optional<std::string> const& password,
                        std::string const& drive_name,
                        std::string const& root_permissions,
                        bool create_home,
                        std::string const& searchbase,
                        boost::optional<std::string> const& filter,
                        boost::optional<std::string> const& object_class,
                        std::string const& mountpoint,
                        bool deny_write,
                        bool deny_storage);


      /*---------------------.
      | Mode: populate hub.  |
      `---------------------*/
      Mode<LDAP,
           void (decltype(cli::server)::Formal<std::string const&>,
                 decltype(cli::domain)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::searchbase)::Formal<std::string const&>,
                 decltype(cli::filter = boost::optional<std::string>()),
                 decltype(cli::object_class = boost::optional<std::string>()),
                 decltype(cli::username_pattern = std::string()),
                 decltype(cli::email_pattern = std::string()),
                 decltype(cli::fullname_pattern = std::string())),
           decltype(modes::mode_populate_hub)>
      populate_hub;
      void
      mode_populate_hub(std::string const& server,
                        std::string const& domain,
                        std::string const& user,
                        boost::optional<std::string> const& password,
                        std::string const& searchbase,
                        boost::optional<std::string> const& filter,
                        boost::optional<std::string> const& object_class,
                        std::string const& username_pattern,
                        std::string const& email_pattern,
                        std::string const& fullname_pattern);

      /*-------------------------.
      | Mode: populate network.  |
      `-------------------------*/
      Mode<LDAP,
           void (decltype(cli::server)::Formal<std::string const&>,
                 decltype(cli::domain)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::searchbase)::Formal<std::string const&>,
                 decltype(cli::filter = boost::optional<std::string>()),
                 decltype(cli::object_class = boost::optional<std::string>()),
                 decltype(cli::mountpoint)::Formal<std::string const&>,
                 decltype(cli::deny_write = false),
                 decltype(cli::deny_storage = false)),
           decltype(modes::mode_populate_network)>
      populate_network;
      void
      mode_populate_network(std::string const& server,
                            std::string const& domain,
                            std::string const& user,
                            boost::optional<std::string> const& password,
                            std::string const& network_name,
                            std::string const& searchbase,
                            boost::optional<std::string> const& filter,
                            boost::optional<std::string> const& object_class,
                            std::string const& mountpoint,
                            bool deny_write,
                            bool deny_storage);
    };
  }
}
