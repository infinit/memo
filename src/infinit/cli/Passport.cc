#include <infinit/cli/Passport.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/Passport.hh>

ELLE_LOG_COMPONENT("cli.passport");

namespace infinit
{
  using Passport = Infinit::Passport;

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
        ifnt.passport_save(passport);
      if (push || push_passport)
        ifnt.hub_push(
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network_name = ifnt.qualified_name(network_name_, owner);
      if (pull)
        ifnt.hub_delete(
          elle::sprintf("networks/%s/passports/%s", network_name, user_name),
          "passport for",
          user_name,
          owner,
          true);
      ifnt.passport_delete(network_name, user_name);
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network_name = ifnt.qualified_name(network_name_, owner);
      auto out = cli.get_output(output);
      auto passport = ifnt.passport_get(network_name, user_name);
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
      qualified_name(infinit::cli::Memo& cli,
                     boost::optional<std::string> const& name,
                     boost::optional<infinit::User const&> owner = {})
      {
        auto& ifnt = cli.infinit();
        if (name)
        {
          // Avoid calling self_user, unless really needed.  That avoids
          // that the current user need to exist.
          if (ifnt.is_qualified_name(*name))
            return *name;
          else
            return ifnt.qualified_name(*name,
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      network_name = qualified_name(cli, network_name, owner);
      if (network_name && user_name)
      {
        auto passport = ifnt.hub_fetch<infinit::Infinit::Passport>(
          elle::sprintf("networks/%s/passports/%s",
                        network_name.get(), user_name.get()),
          "passport for",
          user_name.get(),
          owner);
        ifnt.passport_save(passport, true);
      }
      // Fetch all network passports if owner else fetch just the user's passport.
      else if (network_name)
      {
        auto owner_name = ifnt.owner_name(*network_name);
        if (owner_name == owner.name)
        {
          auto res = ifnt.hub_fetch_json(
            elle::sprintf("networks/%s/passports", network_name.get()),
            "passports for",
            network_name.get(),
            owner);
          auto json = boost::any_cast<elle::json::Object>(res);
          for (auto const& user_passport: json)
          {
            auto s = elle::serialization::json::SerializerIn(user_passport.second, false);
            auto passport = s.deserialize<infinit::Infinit::Passport>();
            ifnt.passport_save(passport, true);
          }
        }
        else
        {
          auto passport = ifnt.hub_fetch<infinit::Infinit::Passport>(elle::sprintf(
            "networks/%s/passports/%s", network_name.get(), owner.name),
            "passport for",
            network_name.get(),
            owner);
          ifnt.passport_save(passport, true);
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
          = std::unordered_map<std::string, std::vector<infinit::Infinit::Passport>>;
        auto res = ifnt.hub_fetch<Passports>(
            elle::sprintf("users/%s/passports", owner.name),
            "passports for user",
            owner.name,
            owner);
        for (auto const& passport: res["passports"])
          ifnt.passport_save(passport, true);
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
      auto& ifnt = cli.infinit();
      auto input = cli.get_input(input_name);
      auto passport = elle::serialization::json::deserialize<infinit::Infinit::Passport>
        (*input, false);
      ifnt.passport_save(passport);
      auto user_name = [&] () -> std::string
        {
          for (auto const& user: ifnt.users_get())
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
      auto& ifnt = cli.infinit();
      network_name = qualified_name(cli, network_name);
      auto passports = ifnt.passports_get(network_name);
      if (cli.script())
      {
        auto l = elle::json::Array{};
        for (auto const& pair: passports)
          l.emplace_back(elle::json::Object
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network_name = ifnt.qualified_name(network_name_, owner);
      ifnt.hub_delete(
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network_name = ifnt.qualified_name(network_name_, owner);
      auto passport = ifnt.passport_get(network_name, user_name);
      ifnt.hub_push(
          elle::sprintf("networks/%s/passports/%s", network_name, user_name),
          "passport",
          elle::sprintf("%s: %s", network_name, user_name),
          passport,
          owner);
    }
  }
}
