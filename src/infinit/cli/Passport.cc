#include <infinit/cli/Passport.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>

ELLE_LOG_COMPONENT("cli.passport");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Passport::Passport(Infinit& infinit)
      : Entity(infinit)
      , create(
        "Create a passport for a user to a network",
        das::cli::Options(),
        this->bind(modes::mode_create,
                   cli::network,
                   cli::user,
                   cli::push_passport = false,
                   cli::push = false,
                   cli::deny_write = false,
                   cli::deny_storage = false,
                   cli::allow_create_passport = false,
                   cli::output = boost::none))
    {}

    /*---------------.
    | Mode: create.  |
    `---------------*/

    void
    Passport::mode_create(std::string const& network_name,
                          std::string const& user_name,
                          bool push_passport,
                          bool push,
                          bool deny_write,
                          bool deny_storage,
                          bool allow_create_passport,
                          boost::optional<std::string> const& output)
    {
    }
  }
}
