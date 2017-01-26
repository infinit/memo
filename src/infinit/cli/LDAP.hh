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
        = decltype(elle::meta::list(cli::populate_network));

      /*-------------------------.
      | Mode: populate Network.  |
      `-------------------------*/
      Mode<decltype(binding(modes::mode_populate_network,
                            cli::server,
                            cli::domain,
                            cli::user,
                            cli::password = boost::none,
                            cli::network,
                            cli::searchbase,
                            cli::filter = boost::none,
                            cli::object_class = boost::none,
                            cli::mountpoint,
                            cli::deny_write = false,
                            cli::deny_storage = false))>
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
