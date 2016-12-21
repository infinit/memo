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
    class Passport
      : public Entity<Passport>
    {
    public:
      Passport(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create));

      // Create.
      Mode<decltype(binding(modes::mode_create,
                            cli::network,
                            cli::user,
                            cli::push_passport = false,
                            cli::push = false,
                            cli::deny_write = false,
                            cli::deny_storage = false,
                            cli::allow_create_passport = false,
                            cli::output = boost::none))>
      create;
      void
      mode_create(std::string const& network_name,
                  std::string const& user_name,
                  bool push_passport = false,
                  bool push = false,
                  bool deny_write = false,
                  bool deny_storage = false,
                  bool allow_create_passport = false,
                  boost::optional<std::string> const& output = {});
    };
  }
}
