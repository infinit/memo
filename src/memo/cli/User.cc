#include <memo/cli/User.hh>

#include <iostream>

#include <boost/range/adaptor/transformed.hpp>

#include <elle/algorithm.hh>

#include <elle/cryptography/rsa/pem.hh>

#include <elle/reactor/http/url.hh>

#include <memo/LoginCredentials.hh>
#include <memo/cli/Memo.hh>

ELLE_LOG_COMPONENT("cli.user");

namespace bfs = boost::filesystem;

using boost::adaptors::transformed;

namespace memo
{
  namespace cli
  {
    using PublicUser = elle::das::Model<
      memo::User,
      decltype(elle::meta::list(
                 memo::symbols::name,
                 memo::symbols::description,
                 memo::symbols::fullname,
                 memo::symbols::public_key,
                 memo::symbols::ldap_dn))>;

    using PublicUserPublish = elle::das::Model<
      memo::User,
      decltype(elle::meta::list(
                 memo::symbols::name,
                 memo::symbols::description,
                 memo::symbols::email,
                 memo::symbols::fullname,
                 memo::symbols::public_key,
                 memo::symbols::ldap_dn))>;

    User::User(Memo& memo)
      : Object(memo)
      , create(*this,
               "Create a user",
               cli::name = memo.default_user_name(),
               cli::description = boost::none,
               cli::key = boost::none,
               cli::email = boost::none,
               cli::fullname = boost::none,
               cli::password = boost::none,
               cli::ldap_name = boost::none,
               cli::output = boost::none,
               cli::push_user = false,
               cli::push = false,
               cli::full = false)
      , delete_(*this,
                "Delete local user",
                cli::name = memo.default_user_name(),
                cli::pull = false,
                cli::purge = false,
                cli::force = false)
      , export_(*this,
                "Export local user",
                cli::name = memo.default_user_name(),
                cli::full = false,
                cli::output = boost::none)
      , fetch(*this,
              "Fetch users from {hub}",
              cli::name =
                std::vector<std::string>{memo.default_user_name()},
              cli::no_avatar = false)
      , hash(*this,
             "Get short hash of user's key",
             cli::name = memo.default_user_name())
      , import(*this,
               "Import local user",
               cli::input = boost::none)
      , list(*this, "List local users")
      , login(*this,
              "Login user to {hub}",
              cli::name = memo.default_user_name(),
              cli::password = boost::none)
      , pull(*this,
             "Pull a user from {hub}",
             cli::name = memo.default_user_name(),
             cli::purge = false)
      , push(*this,
             "Push a user from {hub}",
             cli::name = memo.default_user_name(),
             cli::email = boost::none,
             cli::fullname = boost::none,
             cli::password = boost::none,
             cli::avatar = boost::none,
             cli::full = false)
      , signup(*this,
               "Create and push a user to {hub}",
               cli::name = memo.default_user_name(),
               cli::description = boost::none,
               cli::key = boost::none,
               cli::email = boost::none,
               cli::fullname = boost::none,
               cli::password = boost::none,
               cli::ldap_name = boost::none,
               cli::full = false)
    {}

    namespace
    {
      template <typename Buffer>
      void
      save_avatar(User& api,
                  std::string const& name,
                  Buffer const& buffer)
      {
        // XXX: move to memo::avatar_save maybe ?
        bfs::ofstream f;
        bool existed = memo::Memo::_open_write(
          f, api.cli().backend()._avatar_path(name),
          name, "avatar", true, std::ios::out | std::ios::binary);
        f.write(reinterpret_cast<char const*>(buffer.contents()), buffer.size());
        api.cli().report_action(existed ? "updated" : "saved",
                                "avatar for", name, "locally");
      }

      void
      upload_avatar(User& api,
                    memo::User& self,
                    bfs::path const& avatar_path)
      {
        bfs::ifstream icon;
        memo::Memo::_open_read(icon, avatar_path, self.name, "icon");
        auto s = std::string(
          std::istreambuf_iterator<char>{icon},
          std::istreambuf_iterator<char>{});
        elle::ConstWeakBuffer data(s.data(), s.size());
        auto url = elle::sprintf("users/%s/avatar", self.name);
        api.cli().backend().hub_push_data(
          url, "avatar", self.name, data, "image/jpeg", self);
        save_avatar(api, self.name, data);
      }

      void
      fetch_avatar(User& api, std::string const& name)
      {
        auto url = elle::sprintf("users/%s/avatar", name);
        auto request = api.cli().backend().hub_fetch_request(
          url, "avatar", name);
        if (request->status() == elle::reactor::http::StatusCode::OK)
        {
          auto response = request->response();
          // XXX: Deserialize XML.
          if (response.size() == 0 || response[0] == '<')
            throw MissingResource(
              elle::sprintf("avatar for %s not found on %s", name, beyond()));
          save_avatar(api, name, response);
        }
      }

      void
      pull_avatar(User& api, memo::User& self)
      {
        auto url = elle::sprintf("users/%s/avatar", self.name);
        api.cli().backend().hub_delete(url, "avatar", self.name, self);
      }

      memo::User
      create_user(User& api,
                  std::string const& name,
                  boost::optional<std::string> keys_file,
                  boost::optional<std::string> email,
                  boost::optional<std::string> fullname,
                  boost::optional<std::string> ldap_name,
                  boost::optional<std::string> description)
      {
        if (email && !validate_email(*email))
          elle::err<CLIError>("invalid email address: %s", *email);
        auto keys = [&]
        {
          if (keys_file)
          {
            auto passphrase = Memo::read_passphrase();
            return elle::cryptography::rsa::pem::import_keypair(
                *keys_file, passphrase);
          }
          else
          {
            api.cli().report("generating RSA keypair");
            return elle::cryptography::rsa::keypair::generate(2048);
          }
        }();
        return {name, keys, email, fullname, ldap_name, description};
      }

      void
      user_push(User& api,
                memo::User& user,
                boost::optional<std::string> password,
                bool full)
      {
        if (full)
        {
          if (!password)
            password = Memo::read_password();
          if (!user.ldap_dn)
            user.password_hash = Memo::hub_password_hash(*password);
          api.cli().backend().hub_push<elle::das::Serializer<PrivateUserPublish>>(
            "user", user.name, user, user);
        }
        else
        {
          if (password)
            elle::err<CLIError>
              ("password is only used when pushing a full user");
          api.cli().backend().hub_push<elle::das::Serializer<PublicUserPublish>>(
            "user", user.name, user, user, !api.cli().script());
        }
      }
    }

    /*------.
    | Modes |
    `------*/

    void
    User::mode_create(std::string const& name,
                      boost::optional<std::string> description,
                      boost::optional<std::string> key,
                      boost::optional<std::string> email,
                      boost::optional<std::string> fullname,
                      boost::optional<std::string> password,
                      boost::optional<std::string> ldap_name,
                      boost::optional<std::string> path,
                      bool push_user,
                      bool push,
                      bool full)
    {
      ELLE_TRACE_SCOPE("create");
      push = push || push_user;
      if (!push)
      {
        if (ldap_name)
          elle::err<CLIError>(
            "LDAP can only be used with the Hub, add --push");
        if (full)
          elle::err<CLIError>(
            "--full can only be used with the Hub, add --push");
        if (password)
          elle::err<CLIError>(
            "--password can only be used with the Hub, add --push");
      }
      if (ldap_name && !full)
        elle::err<CLIError>("LDAP user creation requires --full");
      memo::User user =
        create_user(*this, name, key, email, fullname, ldap_name, description);
      if (auto output = this->cli().get_output(path, false))
      {
        memo::Memo::save(*output, user);
        this->cli().report_exported(std::cout, "user", user.name);
      }
      else
        this->cli().backend().user_save(user);
      if (push)
        user_push(*this, user, password, full);
    }

    void
    User::mode_delete(std::string const& name,
                      bool pull,
                      bool purge,
                      bool force)
    {
      ELLE_TRACE_SCOPE("delete");
      auto& ifnt = this->cli().backend();
      auto user = ifnt.user_get(name);
      if (user.private_key && !this->cli().script() && !force)
      {
        std::string res;
        {
          std::cout
            << "WARNING: The local copy of the user's private key will be removed.\n"
            << "WARNING: You will no longer be able to perform actions on " << beyond() << "\n"
            << "WARNING: for this user.\n"
            << "\n"
            << "Confirm the name of the user you would like to delete: ";
          std::getline(std::cin, res);
        }
        if (res != user.name)
          elle::err("Aborting...");
      }
      if (pull)
      {
        try
        {
          auto self = this->cli().as_user();
          ifnt.hub_delete("user", name, self, true, purge);
        }
        catch (MissingLocalResource const& e)
        {
          elle::err("unable to pull user, ensure the user has been set "
                    "using --as or MEMO_USER");
        }
      }
      if (purge)
      {
        for (auto const& pair: ifnt.passports_get())
        {
          auto network = pair.first.network();
          if (ifnt.owner_name(network) != user.name
              && pair.second != user.name)
            continue;
          ifnt.passport_delete(network, pair.second);
        }
        for (auto const& network_: ifnt.networks_get(user))
        {
          auto network = network_.name;
          if (ifnt.owner_name(network) == user.name)
            ifnt.network_delete(network, user, true);
          else
            ifnt.network_unlink(network, user);
        }
      }
      ifnt.avatar_delete(user);
      ifnt.user_delete(user);
    }

    void
    User::mode_export(std::string const& name,
                      bool full,
                      boost::optional<std::string> path)
    {
      ELLE_TRACE_SCOPE("export");
      auto user = this->cli().backend().user_get(name);
      auto output = this->cli().get_output(path);
      auto avatar = this->cli().avatar_path(name);
      if (avatar)
        user.avatar_path = avatar->string();
      if (full)
      {
        if (!this->cli().script())
        {
          elle::fprintf(std::cerr, "WARNING: you are exporting the user \"%s\" "
                        "including the private key\n", name);
          elle::fprintf(std::cerr, "WARNING: anyone in possession of this "
                        "information can impersonate that user\n");
          elle::fprintf(std::cerr, "WARNING: if you mean to export your user for "
                        "someone else, remove the --full flag\n");
        }
        elle::serialization::json::serialize(user, *output, false);
      }
      else
      {
        elle::serialization::json::serialize<
          elle::das::Serializer<memo::User, PublicUser>>(user, *output, false);
      }
      this->cli().report_exported(std::cout, "user", user.name);
    }

    void
    User::mode_fetch(std::vector<std::string> const& user_names,
                     bool no_avatar)
    {
      ELLE_TRACE_SCOPE("fetch");
      for (auto const& name: user_names)
      {
        auto avatar = [&] () {
          if (!no_avatar)
          {
            try
            {
              fetch_avatar(*this, name);
            }
            catch (elle::Error const&)
            {}
          }
        };
        try
        {
          auto user = this->cli().backend().hub_fetch<memo::User>(
            "user", elle::reactor::http::url_encode(name));
          this->cli().backend().user_save(std::move(user));
          avatar();
        }
        catch (ResourceAlreadyFetched const& e)
        {
          avatar();
          throw;
        }
      }
    }

    void
    User::mode_hash(std::string const& name)
    {
      ELLE_TRACE_SCOPE("hash");
      auto user = this->cli().backend().user_get(name);
      auto key_hash = memo::model::doughnut::short_key_hash(user.public_key);
      if (this->cli().script())
      {
        auto res = elle::json::Json {
          {name, key_hash},
        };
        elle::json::write(std::cout, res);
      }
      else
        elle::fprintf(std::cout, "%s: %s\n", name, key_hash);
    }

    void
    User::mode_import(boost::optional<std::string> const& path)
    {
      ELLE_TRACE_SCOPE("import");
      auto input = this->cli().get_input(path);
      auto user =
        elle::serialization::json::deserialize<memo::User>(*input, false);
      this->cli().backend().user_save(user);
      this->cli().report_imported("user", user.name);
    }

    void
    User::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto users = this->cli().backend().users_get();
      boost::optional<memo::User> self;
      try
      {
        self.emplace(this->cli().as_user());
      }
      catch (MissingLocalResource const&)
      {}
      std::sort(users.begin(), users.end(),
                [] (memo::User const& lhs, memo::User const& rhs)
                { return lhs.name < rhs.name; });
      if (this->cli().script())
      {
        auto l = elle::json::Json(
          users | transformed(
            [](auto const& user) {
              auto res = elle::json::Json
                {
                  {"name", static_cast<std::string>(user.name)},
                  {"has_private_key",  bool(user.private_key)},
                };
              if (user.description)
                res["description"] = *user.description;
              return res;
            }));
        elle::json::write(std::cout, l);
      }
      else
      {
        bool user_in_list = self && elle::contains(users, self);
        for (auto const& user: users)
        {
          std::cout << (self && user == self ? "* " : user_in_list ? "  " : "")
                    << user.name;
          if (user.description)
            std::cout << " \"" << *user.description << "\"";
          std::cout << ": public";
          if (user.private_key)
            std::cout << "/private keys";
          else
            std::cout << " key only";
          std::cout << std::endl;
        }
      }
    }

    void
    User::mode_login(std::string const& name,
                     boost::optional<std::string> const& password)
    {
      ELLE_TRACE_SCOPE("login");
      auto pass = password.value_or(Memo::read_password());
      auto hashed_pass = Memo::hub_password_hash(pass);
      auto c = LoginCredentials{name, hashed_pass, pass};
      auto json = this->cli().backend().hub_login(name, c);
      elle::serialization::json::SerializerIn input(json, false);
      auto user = input.deserialize<memo::User>();
      this->cli().backend().user_save(user, true);
    }

    void
    User::mode_pull(std::string const& name, bool purge)
    {
      ELLE_TRACE_SCOPE("pull");
      auto self = this->cli().as_user();
      this->cli().backend().hub_delete("user", name, self, false, purge);
    }

    void
    User::mode_push(std::string const& name,
                    boost::optional<std::string> email,
                    boost::optional<std::string> fullname,
                    boost::optional<std::string> password,
                    boost::optional<std::string> avatar,
                    bool full)
    {
      ELLE_TRACE_SCOPE("push");
      auto user = this->cli().backend().user_get(name);
      // FIXME: why does push provide a way to update those fields?
      if (email || fullname)
      {
        if (email)
          user.email = *email;
        if (fullname)
          user.fullname = *fullname;
        this->cli().backend().user_save(user, true);
      }
      user_push(*this, user, password, full);
      if (avatar)
      {
        if (!avatar->empty())
        {
          if (!bfs::exists(*avatar))
            elle::err("avatar file doesn't exist: %s", *avatar);
          // Also saves avatar locally.
          upload_avatar(*this, user, *avatar);
        }
        else
          pull_avatar(*this, user);
      }
    }

    void
    User::mode_signup(std::string const& name,
                      boost::optional<std::string> description,
                      boost::optional<std::string> key,
                      boost::optional<std::string> email,
                      boost::optional<std::string> fullname,
                      boost::optional<std::string> password,
                      boost::optional<std::string> ldap_name,
                      bool full)
    {
      ELLE_TRACE_SCOPE("signup");
      if (ldap_name && !full)
        elle::err<CLIError>("LDAP user creation requires --full");
      auto user = create_user(*this,
                              name,
                              key,
                              email,
                              fullname,
                              ldap_name,
                              description);
      auto user_exists = false;
      try
      {
        this->cli().backend().user_get(name);
        user_exists = true;
      }
      catch (elle::Error const&)
      {
        user_push(*this, user, password, full);
        this->cli().backend().user_save(user);
      }
      if (user_exists)
        elle::err("user %s already exists locally", name);
    }
  }
}
