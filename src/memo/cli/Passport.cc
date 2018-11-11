#include <memo/cli/Passport.hh>

#include <memo/cli/Memo.hh>
#include <memo/model/doughnut/Passport.hh>

ELLE_LOG_COMPONENT("cli.passport");

namespace memo
{
  using Passport = Memo::Passport;

  namespace cli
  {
    Passport::Passport(Memo& memo)
      : Object(memo)
      , create(*this,
               "Create a passport for a user to a network",
               elle::das::cli::Options(),
               cli::network,
               cli::user,
               cli::push_passport = false,
               cli::push = false,
               cli::deny_write = false,
               cli::deny_storage = false,
               cli::allow_create_passport = false,
               cli::output = boost::none)
      , delete_(*this,
                "Locally delete a passport",
                elle::das::cli::Options(),
                cli::network,
                cli::user,
                cli::pull = false)
      , export_(*this,
                "Export a user's network passport",
                elle::das::cli::Options(),
                cli::network,
                cli::user,
                cli::output = boost::none)
      , fetch(*this,
              "Fetch a user's network passport from {hub}",
              elle::das::cli::Options(),
              cli::network = boost::none,
              cli::user = boost::none)
      , import(*this,
               "Import a passport for a user to a network",
               elle::das::cli::Options(),
               cli::input = boost::none)
      , list(*this,
             "List all local passports",
             elle::das::cli::Options(),
             cli::network = boost::none)
      , pull(*this,
             "Remove a user's network passport from {hub}",
             elle::das::cli::Options(),
             cli::network,
             cli::user)
      , push(*this,
             "Push a user's network passport to {hub}",
             elle::das::cli::Options(),
             cli::network,
             cli::user)
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
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto network = memo.network_descriptor_get(network_name, owner, true);
      auto user = memo.user_get(user_name);
      if (owner.public_key != network.owner)
        std::cerr
          << "NOTICE: your key is not that of the owner of the network.\n"
          << "A passport for you with the 'sign' permission needs to be \n"
          << "pushed to the network using memo acl --register.\n";
      auto passport = memo::model::doughnut::Passport(
        user.public_key,
        network.name,
        elle::cryptography::rsa::KeyPair(owner.public_key,
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
        memo.passport_save(passport);
      if (push || push_passport)
        memo.hub_push(
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
    Passport::mode_delete(std::string const& network_name_,
                          std::string const& user_name,
                          bool pull)
    {
      ELLE_TRACE_SCOPE("delete");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto network_name = memo.qualified_name(network_name_, owner);
      if (pull)
        memo.hub_delete(
          elle::sprintf("networks/%s/passports/%s", network_name, user_name),
          "passport for",
          user_name,
          owner,
          true);
      memo.passport_delete(network_name, user_name);
    }

    /*---------------.
    | Mode: export.  |
    `---------------*/
    void
    Passport::mode_export(std::string const& network_name_,
                          std::string const& user_name,
                          boost::optional<std::string> const& output)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto network_name = memo.qualified_name(network_name_, owner);
      auto out = cli.get_output(output);
      auto passport = memo.passport_get(network_name, user_name);
      elle::serialization::json::serialize(passport, *out, false);
      cli.report_exported(*out, "passport",
                          elle::sprintf("%s: %s", network_name, user_name));
    }

    /*--------.
    | Fetch.  |
    `--------*/

    namespace
    {
      boost::optional<std::string>
      qualified_name(memo::cli::Memo& cli,
                     boost::optional<std::string> const& name,
                     boost::optional<memo::User const&> owner = {})
      {
        auto& memo = cli.backend();
        if (name)
        {
          // Avoid calling self_user, unless really needed.  That avoids
          // that the current user need to exist.
          if (memo.is_qualified_name(*name))
            return *name;
          else
            return memo.qualified_name(*name,
                                       owner ? *owner : cli.as_user());
        }
        else
          return boost::none;
      }
    }

    void
    Passport::mode_fetch(boost::optional<std::string> network_name,
                         boost::optional<std::string> const& user_name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      network_name = qualified_name(cli, network_name, owner);
      if (network_name && user_name)
      {
        auto passport = memo.hub_fetch<memo::Memo::Passport>(
          elle::sprintf("networks/%s/passports/%s",
                        network_name.get(), user_name.get()),
          "passport for",
          user_name.get(),
          owner);
        memo.passport_save(passport, true);
      }
      // Fetch all network passports if owner else fetch just the user's passport.
      else if (network_name)
      {
        auto owner_name = memo.owner_name(*network_name);
        if (owner_name == owner.name)
        {
          auto json = memo.hub_fetch_json(
            elle::sprintf("networks/%s/passports", network_name.get()),
            "passports for",
            network_name.get(),
            owner);
          for (auto const& user_passport: json)
          {
            auto s =
              elle::serialization::json::SerializerIn(user_passport, false);
            auto passport = s.deserialize<memo::Memo::Passport>();
            memo.passport_save(passport, true);
          }
        }
        else
        {
          auto passport = memo.hub_fetch<memo::Memo::Passport>(elle::sprintf(
            "networks/%s/passports/%s", network_name.get(), owner.name),
            "passport for",
            network_name.get(),
            owner);
          memo.passport_save(passport, true);
        }
      }
      else if (user_name && user_name.get() != owner.name)
      {
        elle::err<CLIError>("use the --as to fetch passports for another user");
      }
      // Fetch owner passports.
      else
      {
        using Passports
          = std::unordered_map<std::string, std::vector<memo::Memo::Passport>>;
        auto res = memo.hub_fetch<Passports>(
            elle::sprintf("users/%s/passports", owner.name),
            "passports for user",
            owner.name,
            owner);
        for (auto const& passport: res["passports"])
          memo.passport_save(passport, true);
      }
    }

    /*---------.
    | Import.  |
    `---------*/
    void
    Passport::mode_import(boost::optional<std::string> const& input_name)
    {
      ELLE_TRACE_SCOPE("import");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto input = cli.get_input(input_name);
      auto passport = elle::serialization::json::deserialize<memo::Memo::Passport>
        (*input, false);
      memo.passport_save(passport);
      auto user_name = [&] () -> std::string
        {
          for (auto const& user: memo.users_get())
            if (user.public_key == passport.user())
              return user.name;
          return {};
        }();
      cli.report_imported("passport",
                          elle::sprintf("%s: %s", passport.network(), user_name));
    }

    /*-------.
    | List.  |
    `-------*/
    void
    Passport::mode_list(boost::optional<std::string> network_name)
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      network_name = qualified_name(cli, network_name);
      auto passports = memo.passports_get(network_name);
      if (cli.script())
      {
        auto l = elle::json::Json::array();
        for (auto const& pair: passports)
          l.emplace_back(elle::json::Json
                         {
                           {"network", pair.first.network()},
                           {"user", pair.second},
                         });
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& pair: passports)
          std::cout << pair.first.network() << ": " << pair.second << std::endl;
    }

    /*-------.
    | Pull.  |
    `-------*/
    void
    Passport::mode_pull(std::string const& network_name_,
                        std::string const& user_name)
    {
      ELLE_TRACE_SCOPE("pull");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto network_name = memo.qualified_name(network_name_, owner);
      memo.hub_delete(
          elle::sprintf("networks/%s/passports/%s", network_name, user_name),
          "passport for",
          user_name,
          owner);
    }

    /*-------.
    | Push.  |
    `-------*/
    void
    Passport::mode_push(std::string const& network_name_,
                        std::string const& user_name)
    {
      ELLE_TRACE_SCOPE("push");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto network_name = memo.qualified_name(network_name_, owner);
      auto passport = memo.passport_get(network_name, user_name);
      memo.hub_push(
          elle::sprintf("networks/%s/passports/%s", network_name, user_name),
          "passport",
          elle::sprintf("%s: %s", network_name, user_name),
          passport,
          owner);
    }
  }
}
