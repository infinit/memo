#pragma once

#include <das/cli.hh>

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
           decltype(modes::mode_drive_invite),
           decltype(cli::server),
           decltype(cli::domain),
           decltype(cli::user),
           decltype(cli::password = boost::none),
           decltype(cli::drive),
           decltype(cli::root_permissions = "rw"),
           decltype(cli::create_home = false),
           decltype(cli::searchbase),
           decltype(cli::filter = boost::none),
           decltype(cli::object_class = boost::none),
           decltype(cli::mountpoint),
           decltype(cli::deny_write = false),
           decltype(cli::deny_storage = false)>
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
           decltype(modes::mode_populate_hub),
           decltype(cli::server),
           decltype(cli::domain),
           decltype(cli::user),
           decltype(cli::password = boost::none),
           decltype(cli::searchbase),
           decltype(cli::filter = boost::none),
           decltype(cli::object_class = boost::none),
           decltype(cli::username_pattern = "$(cn)%"),
           decltype(cli::email_pattern = "$(mail)"),
           decltype(cli::fullname_pattern = "$(cn)")>
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
           decltype(modes::mode_populate_network),
           decltype(cli::server),
           decltype(cli::domain),
           decltype(cli::user),
           decltype(cli::password = boost::none),
           decltype(cli::network),
           decltype(cli::searchbase),
           decltype(cli::filter = boost::none),
           decltype(cli::object_class = boost::none),
           decltype(cli::mountpoint),
           decltype(cli::deny_write = false),
           decltype(cli::deny_storage = false)>
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
