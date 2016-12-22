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
      , delete_(
        "Locally delete a passport",
        das::cli::Options(),
        this->bind(modes::mode_delete,
                   cli::network,
                   cli::user,
                   cli::pull = false))
      , export_(
        "Export a user's network passport",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::network,
                   cli::user,
                   cli::output = boost::none))
      , fetch(
        "Fetch a user's network passport from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   cli::network,
                   cli::user))
      , import(
        "Import a passport for a user to a network",
        das::cli::Options(),
        this->bind(modes::mode_import,
                   cli::input = boost::none))
      , list(
        "List all local passports",
        das::cli::Options(),
        this->bind(modes::mode_list,
                   cli::network = boost::none))
      , pull(
        "Remove a user's network passport from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_pull,
                   cli::network,
                   cli::user))
      , push(
        "Push a user's network passport to {hub}",
        das::cli::Options(),
        this->bind(modes::mode_push,
                   cli::network,
                   cli::user))
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
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_descriptor_get(network_name, owner, true);
      auto user = ifnt.user_get(user_name);
      if (owner.public_key != network.owner)
        std::cerr
          << "NOTICE: your key is not that of the owner of the network.\n"
          << "A passport for you with the 'sign' permission needs to be \n"
          << "pushed to the network using infinit-acl --register.\n";
      auto passport = infinit::model::doughnut::Passport(
        user.public_key,
        network.name,
        infinit::cryptography::rsa::KeyPair(owner.public_key,
                                            owner.private_key.get()),
        owner.public_key != network.owner,
        !deny_write,
        !deny_storage,
        allow_create_passport);
      if (output)
      {
        auto out = this->cli().get_output(*output);
        elle::serialization::json::serialize(passport, *out, false);
        cli.report_action_output(*out, "wrote", "passport for", network.name);
      }
      else
      {
        ifnt.passport_save(passport);
        cli.report_created("passport",
                           elle::sprintf("%s: %s", network.name, user_name));
      }
      if (push || push_passport)
        ifnt.beyond_push(
          elle::sprintf("networks/%s/passports/%s", network.name, user.name),
          "passport",
          elle::sprintf("%s: %s", network.name, user.name),
          passport,
          owner);
    }


    /*---------.
    | Delete.  |
    `---------*/
    void
    Passport::mode_delete(std::string const& network_name,
                          std::string const& user_name,
                          bool pull)
    {}

    /*---------------.
    | Mode: export.  |
    `---------------*/
    void
    Passport::mode_export(std::string const& network_name,
                          std::string const& user_name,
                          boost::optional<std::string> const& output)
    {}

    /*--------.
    | Fetch.  |
    `--------*/
    void
    Passport::mode_fetch(std::string const& network_name,
                         std::string const& user_name)
    {}

    /*---------.
    | Import.  |
    `---------*/
    void
    Passport::mode_import(boost::optional<std::string> const& input)
    {}

    /*-------.
    | List.  |
    `-------*/
    void
    Passport::mode_list(boost::optional<std::string> const& network_name)
    {}

    /*-------.
    | Pull.  |
    `-------*/
    void
    Passport::mode_pull(std::string const& network_name,
                        std::string const& user_name)
    {}

    /*-------.
    | Push.  |
    `-------*/
    void
    Passport::mode_push(std::string const& network_name,
                        std::string const& user_name)
    {}
  }
}
