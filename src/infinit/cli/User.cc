#include <infinit/cli/User.hh>

#include <iostream>

#include <cryptography/rsa/pem.hh>

#include <reactor/http/url.hh>

#include <infinit/LoginCredentials.hh>
#include <infinit/cli/Infinit.hh>

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;
    using PublicUser = das::Model<
      infinit::User,
      decltype(elle::meta::list(
                 infinit::symbols::name,
                 infinit::symbols::description,
                 infinit::symbols::fullname,
                 infinit::symbols::public_key,
                 infinit::symbols::ldap_dn))>;

    using PublicUserPublish = das::Model<
      infinit::User,
      decltype(elle::meta::list(
                 infinit::symbols::name,
                 infinit::symbols::description,
                 infinit::symbols::email,
                 infinit::symbols::fullname,
                 infinit::symbols::public_key,
                 infinit::symbols::ldap_dn))>;

    User::User(Infinit& infinit)
      : Entity(infinit)
      , create(
        "Create a user",
        das::cli::Options(),
        this->bind(modes::mode_create,
                   name = Infinit::default_user_name(),
                   description = boost::none,
                   key = boost::none,
                   cli::email = boost::none,
                   cli::fullname = boost::none,
                   password = boost::none,
                   ldap_name = boost::none,
                   output = boost::none,
                   push_user = false,
                   cli::push = false,
                   full = false))
      , delete_(
        "Delete local user",
        das::cli::Options(),
        this->bind(modes::mode_delete,
                   name = Infinit::default_user_name(),
                   cli::pull = false,
                   purge = false))
      , export_(
        "Export local user",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   name = Infinit::default_user_name(),
                   full = false,
                   output = boost::none))
      , fetch(
        elle::sprintf("Fetch users from %s", beyond()),
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   name = Infinit::default_user_name(),
                   cli::no_avatar = false))
      , hash(
        "Get short hash of user's key",
        das::cli::Options(),
        this->bind(modes::mode_hash,
                   name = Infinit::default_user_name()))
      , import(
        "Import local user",
        das::cli::Options(),
        this->bind(modes::mode_import,
                   cli::input = boost::none))
      , list(
        "List local users",
        das::cli::Options(),
        this->bind(modes::mode_list))
      , login(
        elle::sprintf("Login user to %s", beyond()),
        das::cli::Options(),
        this->bind(modes::mode_login,
                   name = Infinit::default_user_name(),
                   password = boost::none))
      , pull(
        elle::sprintf("Pull a user from %s", beyond()),
        das::cli::Options(),
        this->bind(modes::mode_pull,
                   name = Infinit::default_user_name(),
                   purge = false))
      , push(
        elle::sprintf("Push a user from %s", beyond()),
        das::cli::Options(),
        this->bind(modes::mode_push,
                   name = Infinit::default_user_name(),
                   cli::email = boost::none,
                   fullname = boost::none,
                   password = boost::none,
                   cli::avatar = boost::none,
                   full = false))
      , signup(
        elle::sprintf("Create and push a user to %s", beyond()),
        das::cli::Options(),
        this->bind(modes::mode_signup,
                   name = Infinit::default_user_name(),
                   description = boost::none,
                   key = boost::none,
                   cli::email = boost::none,
                   cli::fullname = boost::none,
                   password = boost::none,
                   ldap_name = boost::none,
                   full = false))
    {}

    template <typename Buffer>
    static
    void
    save_avatar(User& api,
                std::string const& name,
                Buffer const& buffer)
    {
      boost::filesystem::ofstream f;
      infinit::Infinit::_open_write(
        f, api.cli().infinit()._user_avatar_path(name),
        name, "avatar", true, std::ios::out | std::ios::binary);
      f.write(reinterpret_cast<char const*>(buffer.contents()), buffer.size());
      api.cli().report_action("saved", "avatar", name, "locally");
    }

    static
    void
    upload_avatar(User& api,
                  infinit::User& self,
                  boost::filesystem::path const& avatar_path)
    {
      boost::filesystem::ifstream icon;
      infinit::Infinit::_open_read(icon, avatar_path, self.name, "icon");
      std::string s(
        std::istreambuf_iterator<char>{icon},
        std::istreambuf_iterator<char>{});
      elle::ConstWeakBuffer data(s.data(), s.size());
      auto url = elle::sprintf("users/%s/avatar", self.name);
      switch (infinit::Infinit::beyond_push_data(
                url, "avatar", self.name, data, "image/jpeg", self))
      {
        case infinit::Infinit::PushResult::pushed:
          api.cli().report_action("saved", "avatar", self.name, "remotely");
          break;
        case infinit::Infinit::PushResult::updated:
          api.cli().report_action("updated", "avatar", self.name, "remotely");
          break;
        case infinit::Infinit::PushResult::alreadyPushed:
          api.cli().report_action("already pushed", "avatar", self.name);
      }
      save_avatar(api, self.name, data);
    }

    static
    void
    fetch_avatar(User& api, std::string const& name)
    {
      auto url = elle::sprintf("users/%s/avatar", name);
      auto request = infinit::Infinit::beyond_fetch_data(url, "avatar", name);
      if (request->status() == reactor::http::StatusCode::OK)
      {
        auto response = request->response();
        // XXX: Deserialize XML.
        if (response.size() == 0 || response[0] == '<')
          throw MissingResource(
            elle::sprintf("avatar for %s not found on %s", name, beyond()));
        save_avatar(api, name, response);
      }
    }

    static
    void
    pull_avatar(infinit::User& self)
    {
      auto url = elle::sprintf("users/%s/avatar", self.name);
      infinit::Infinit::beyond_delete(url, "avatar", self.name, self);
    }

    static
    infinit::User
    create_user(User& api,
                std::string const& name,
                boost::optional<std::string> keys_file,
                boost::optional<std::string> email,
                boost::optional<std::string> fullname,
                boost::optional<std::string> ldap_name,
                boost::optional<std::string> description)
    {
      if (email && !validate_email(email.get()))
        elle::err<Error>("invalid email address: %s", email.get());
      auto keys = [&]
      {
        if (keys_file)
        {
          auto passphrase = Infinit::read_passphrase();
          return infinit::cryptography::rsa::pem::import_keypair(
              keys_file.get(), passphrase);
        }
        else
        {
          api.cli().report("generating RSA keypair");
          return infinit::cryptography::rsa::keypair::generate(2048);
        }
      }();
      return infinit::User(name, keys, email, fullname, ldap_name, description);
    }

    using PrivateUserPublish = das::Model<
      infinit::User,
      decltype(elle::meta::list(
                 infinit::symbols::name,
                 infinit::symbols::description,
                 infinit::symbols::email,
                 infinit::symbols::fullname,
                 infinit::symbols::public_key,
                 infinit::symbols::private_key,
                 infinit::symbols::ldap_dn))>;

    static
    void
    user_push(User& api,
              infinit::User& user,
              boost::optional<std::string> password,
              bool full)
    {
      if (full)
      {
        if (!password)
          password = Infinit::read_password();
        if (!user.ldap_dn)
          user.password_hash = Infinit::hub_password_hash(password.get());
        infinit::Infinit::beyond_push<PrivateUserPublish>(
          "user", user.name, user, user);
      }
      else
      {
        if (password)
          elle::err<Error>(
            "password is only used when pushing a full user");
        infinit::Infinit::beyond_push<PublicUserPublish>(
          "user", user.name, user, user, !api.cli().script());
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
      push = push || push_user;
      if (!push)
      {
        if (ldap_name)
          elle::err<Error>(
            "LDAP can only be used with the Hub, add --push");
        if (full)
          elle::err<Error>(
            "--full can only be used with the Hub, add --push");
        if (password)
          elle::err<Error>(
            "--password can only be used with the Hub, add --push");
      }
      if (ldap_name && !full)
        elle::err<Error>("LDAP user creation requires --full");
      infinit::User user =
        create_user(*this, name, key, email, fullname, ldap_name, description);
      if (auto output = this->cli().get_output(path, false))
      {
        infinit::Infinit::save(*output, user);
        this->cli().report_exported(std::cout, "user", user.name);
      }
      else
      {
        this->cli().infinit().user_save(user);
        this->cli().report_action("generated", "user", name, "locally");
      }
      if (push)
        user_push(*this, user, password, full);
    }

    void
    User::mode_delete(std::string const& name,
                      bool pull,
                      bool purge)
    {
      auto& ifnt = this->cli().infinit();
      auto user = ifnt.user_get(name);
      if (user.private_key && !this->cli().script())
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
          throw elle::Error("Aborting...");
      }
      if (pull)
      {
        try
        {
          auto self = this->cli().as_user();
          infinit::Infinit::beyond_delete("user", name, self, true, purge);
        }
        catch (MissingLocalResource const& e)
        {
          throw elle::Error("unable to pull user, ensure the user has been set "
                            "using --as or INFINIT_USER");
        }
      }
      if (purge)
      {
        // XXX Remove volumes and drives that are on network owned by this user.
        // Currently only the owner of a network can create volumes/drives.
        auto owner = [] (std::string const& qualified_name) {
          return qualified_name.substr(0, qualified_name.find("/"));
        };
        for (auto const& drive_: ifnt.drives_get())
        {
          auto drive = drive_.name;
          if (owner(drive) != user.name)
            continue;
          auto drive_path = ifnt._drive_path(drive);
          if (boost::filesystem::remove(drive_path))
            this->cli().report_action("deleted", "drive", drive, "locally");
        }
        for (auto const& volume_: ifnt.volumes_get())
        {
          auto volume = volume_.name;
          if (owner(volume) != user.name)
            continue;
          auto volume_path = ifnt._volume_path(volume);
          if (boost::filesystem::remove(volume_path))
            this->cli().report_action("deleted", "volume", volume, "locally");
        }
        for (auto const& pair: ifnt.passports_get())
        {
          auto network = pair.first.network();
          if (owner(network) != user.name &&
              pair.second != user.name)
          {
            continue;
          }
          auto passport_path = ifnt._passport_path(network, pair.second);
          if (boost::filesystem::remove(passport_path))
            this->cli().report_action("deleted", "passport",
                                      elle::sprintf("%s: %s", network, pair.second),
                                      "locally");
        }
        for (auto const& network_: ifnt.networks_get(user))
        {
          auto network = network_.name;
          if (owner(network) == user.name)
            ifnt.network_delete(network, user, true);
          else
          {
            ifnt.network_unlink(network, user);
            this->cli().report_action("Unlinked", "network", network.name());
          }
        }
      }
      if (auto path = this->cli().avatar_path(name))
        boost::filesystem::remove(path.get());
      auto path = ifnt._user_path(user.name);
      if (boost::filesystem::remove(path))
      {
        this->cli().report_action("deleted", "user", user.name, "locally");
      }
      else
      {
        throw elle::Error(
          elle::sprintf("File for user could not be deleted: %s", path));
      }
    }

    void
    User::mode_export(std::string const& name,
                      bool full,
                      boost::optional<std::string> path)
    {
      auto user = this->cli().infinit().user_get(name);
      auto output = this->cli().get_output(path);
      auto avatar = this->cli().avatar_path(name);
      if (avatar)
        user.avatar_path = avatar.get().string();
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
          das::Serializer<infinit::User, PublicUser>>(user, *output, false);
      }
      this->cli().report_exported(std::cout, "user", user.name);
    }

    void
    User::mode_fetch(std::vector<std::string> const& user_names,
                     bool no_avatar)
    {
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
          auto user = infinit::Infinit::beyond_fetch<infinit::User>(
            "user", reactor::http::url_encode(name));
          this->cli().infinit().user_save(std::move(user));
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
      auto user = this->cli().infinit().user_get(name);
      auto key_hash = infinit::model::doughnut::short_key_hash(user.public_key);
      if (this->cli().script())
      {
        elle::json::Object res;
        res[name] = key_hash;
        elle::json::write(std::cout, res);
      }
      else
        elle::fprintf(std::cout, "%s: %s\n", name, key_hash);
    }

    void
    User::mode_import(boost::optional<std::string> const& path)
    {
      auto input = this->cli().get_input(path);
      auto user =
        elle::serialization::json::deserialize<infinit::User>(*input, false);
      this->cli().infinit().user_save(user);
      this->cli().report_imported("user", user.name);
    }

    void
    User::mode_list()
    {
      auto users = this->cli().infinit().users_get();
      boost::optional<infinit::User> self;
      try
      {
        self.emplace(this->cli().as_user());
      }
      catch (MissingLocalResource const&)
      {}
      std::sort(users.begin(), users.end(),
                [] (infinit::User const& lhs, infinit::User const& rhs)
                { return lhs.name < rhs.name; });
      if (this->cli().script())
      {
        elle::json::Array l;
        for (auto const& user: users)
        {
          elle::json::Object o;
          o["name"] = static_cast<std::string>(user.name);
          o["has_private_key"] = bool(user.private_key);
          if (user.description)
            o["description"] = user.description.get();
          l.push_back(std::move(o));
        }
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& user: users)
        {
          std::cout << (self && user == self ? "* " : "  ") << user.name;
          if (user.description)
            std::cout << " \"" << user.description.get() << "\"";
          std::cout << ": public";
          if (user.private_key)
            std::cout << "/private keys";
          else
            std::cout << " key only";
          std::cout << std::endl;
        }
    }

    void
    User::mode_login(std::string const& name,
                     boost::optional<std::string> const& password)
    {
      auto pass = password.value_or(Infinit::read_password());
      auto hashed_pass = Infinit::hub_password_hash(pass);
      auto c = LoginCredentials{ name, hashed_pass, pass };
      auto json = this->cli().infinit().beyond_login(name, c);
      elle::serialization::json::SerializerIn input(json, false);
      auto user = input.deserialize<infinit::User>();
      this->cli().infinit().user_save(user, true);
      this->cli().report_action("saved", "user", name, "locally");
    }

    void
    User::mode_pull(std::string const& name, bool purge)
    {
      auto self = this->cli().as_user();
      infinit::Infinit::beyond_delete("user", name, self, false, purge);
    }

    void
    User::mode_push(std::string const& name,
                    boost::optional<std::string> email,
                    boost::optional<std::string> fullname,
                    boost::optional<std::string> password,
                    boost::optional<std::string> avatar,
                    bool full)
    {
      auto user = this->cli().infinit().user_get(name);
      // FIXME: why does push provide a way to update those fields ?
      if (email || fullname)
      {
        if (email)
          user.email = email.get();
        if (fullname)
          user.fullname = fullname.get();
        this->cli().infinit().user_save(user, true);
        this->cli().report_updated("user", user.name);
      }
      user_push(*this, user, password, full);
      // FIXME: avatar should probably be stored locally too
      if (avatar)
      {
        if (avatar.get().length() > 0)
        {
          if (!boost::filesystem::exists(avatar.get()))
            elle::err("avatar file doesn't exist: %s", avatar.get());
          upload_avatar(*this, user, avatar.get());
        }
        else
          pull_avatar(user);
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
      std::cerr << "ldap: " << ldap_name << std::endl;
      if (ldap_name && !full)
        elle::err<Error>("LDAPP user creation requires --full");
      infinit::User user = create_user(*this,
                                       name,
                                       key,
                                       email,
                                       fullname,
                                       ldap_name,
                                       description);
      try
      {
        this->cli().infinit().user_get(name);
        elle::err("user %s already exists locally", name);
      }
      catch (elle::Error const&)
      {
        user_push(*this, user, password, full);
        this->cli().infinit().user_save(user);
      }
    }
  }
}
