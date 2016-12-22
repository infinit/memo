#include <infinit/Infinit.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit");

namespace fs = boost::filesystem;

namespace infinit
{
  bool
  Infinit::is_qualified_name(std::string const& name)
  {
    return name.find('/') != std::string::npos;
  }

  std::string
  Infinit::qualified_name(std::string const& name, User const& owner)
  {
    if (is_qualified_name(name))
      return name;
    else
      return elle::sprintf("%s/%s", owner.name, name);
  }

  std::string
  Infinit::owner_name(std::string const& name)
  {
    assert(is_qualified_name(name));
    return name.substr(0, name.find('/'));
  }

  Network
  Infinit::network_get(std::string const& name_,
                       User const& user,
                       bool require_model)
  {
    auto name = qualified_name(name_, user);
    fs::ifstream f;
    // Move linked networks found in the descriptor folder to the correct
    // place.
    bool move = false;
    try
    {
      this->_open_read(f, this->_network_path(name, user), name, "network");
    }
    catch (MissingLocalResource const&)
    {
      this->_open_read(
        f, this->_network_descriptor_path(name), name, "network");
      move = true;
    }
    auto res = elle::serialization::json::deserialize<Network>(f, false);
    std::string const not_linked_msg = elle::sprintf(
      "%s has not yet linked to the network \"%s\" on this device. "
      "Link using infinit-network --link", user.name, res.name);
    if (!res.model && require_model)
      elle::err(not_linked_msg);
    if (move && res.model)
    {
      // Ensure that passed user is same as that linked to network.
      if (!res.user_linked(user))
        elle::err(not_linked_msg);
      fs::ifstream temp_f;
      this->_open_read(
        temp_f, this->_network_descriptor_path(name), name, "network");
      auto temp_net =
        elle::serialization::json::deserialize<Network>(temp_f, false);
      NetworkDescriptor desc(std::move(temp_net));
      auto old_path = this->_network_descriptor_path(res.name);
      auto path = this->_network_path(res.name, user);
      create_directories(path.parent_path());
      fs::rename(old_path, path);
      this->network_save(desc);
    }
    return res;
  }

  std::vector<Network>
  Infinit::networks_get(
    boost::optional<User> self,
    bool require_linked) const
  {
    std::vector<Network> res;
    auto extract =
      [&] (fs::path const& path, bool move) {
      for (fs::recursive_directory_iterator it(path);
           it != fs::recursive_directory_iterator();
           ++it)
      {
        if (!is_regular_file(it->status()) || is_hidden_file(it->path()))
          continue;
        fs::ifstream f;
        this->_open_read(
          f, it->path(), "network", it->path().filename().string());
        auto network =
        elle::serialization::json::deserialize<Network>(f, false);
        if (require_linked && !network.model)
          continue;
        if (move && network.model && self && network.user_linked(*self))
        {
          fs::ifstream temp_f;
          this->_open_read(
            temp_f, it->path(), "network", it->path().filename().string());
          auto temp_net =
            elle::serialization::json::deserialize<Network>(temp_f, false);
          NetworkDescriptor desc(std::move(temp_net));
          auto path = this->_network_path(network.name, *self);
          create_directories(path.parent_path());
          fs::rename(it->path(), path);
          this->network_save(desc);
        }
        // Ignore duplicates.
        if (std::find_if(res.begin(), res.end(),
                         [&network] (Network const& n) {
                           return n.name == network.name;
                         }) == res.end())
        {
          res.push_back(std::move(network));
        }
      }
    };
    if (self)
    {
      // Start by linked networks first.
      extract(this->_networks_path(*self), false);
    }
    // Then network descriptors and possibly old linked networks.
    extract(this->_network_descriptors_path(), true);
    return res;
  }

  std::vector<User>
  Infinit::network_linked_users(std::string const& name_,
                                boost::optional<User> user)
  {
    ELLE_ASSERT(is_qualified_name(name_) || user);
    auto name = name_;
    if (user)
      name = qualified_name(name_, *user);
    auto res = this->users_get();
    res.erase(
      std::remove_if(res.begin(), res.end(),
                     [&] (User const& u)
                     {
                       return !fs::exists(this->_network_path(name, u, false));
                     }),
      res.end());
    return res;
  }

  void
  Infinit::network_unlink(std::string const& name_,
                          User const& user,
                          Reporter report)
  {
    auto name = qualified_name(name_, user);
    auto network = this->network_get(name, user, true);
    auto path = this->_network_path(network.name, user);
    // XXX Should check async cache to make sure that it's empty.
    fs::remove_all(network.cache_dir(user).parent_path());
    if (fs::exists(path))
    {
      boost::system::error_code erc;
      fs::remove(path, erc);
      if (!erc)
      {
        if (report)
          report(network.name);
      }
      else
      {
        ELLE_WARN("Unable to unlink network \"%s\": %s",
                  network.name, erc.message());
      }
    }
  }

  void
  Infinit::network_delete(
    std::string const& name_, User const& user, bool unlink, Reporter report)
  {
    // Ensure if unqualified name is passed, we qualify with passed user.
    auto name = qualified_name(name_, user);
    auto network = this->network_get(name, user, false);
    // Get a list of users who have linked the network.
    auto linked_users = this->network_linked_users(name);
    if (linked_users.size() && !unlink)
    {
      std::vector<std::string> user_names;
      for (auto const& u: linked_users)
        user_names.emplace_back(u.name);
      elle::err("Network is still linked with this device by %s.", user_names);
    }
    boost::system::error_code erc;
    for (auto const& u: linked_users)
    {
      auto linked_path = this->_network_path(name, u);
      fs::remove(linked_path, erc);
      fs::remove_all(network.cache_dir(u).parent_path());
      if (erc)
      {
        ELLE_WARN("Unable to unlink network \"%s\" for \"%s\": %s",
                  name, u.name, erc.message());
      }
      else if (report)
        report(name);
    }
    auto desc_path = this->_network_descriptor_path(name);
    fs::remove(desc_path, erc);
    if (erc)
    {
      ELLE_WARN("Unable to remove network descriptor \"%s\": %s",
                name, erc.message());
    }
    else if (report)
      report(name);
  }

  std::vector<Drive>
  Infinit::drives_get() const
  {
    std::vector<Drive> res;
    for (fs::recursive_directory_iterator it(this->_drives_path());
         it != fs::recursive_directory_iterator();
         ++it)
    {
      if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
      {
        fs::ifstream f;
        this->_open_read(
          f, it->path(), it->path().filename().string(), "drive");
        res.push_back(load<Drive>(f));
      }
    }
    return res;
  }

  NetworkDescriptor
  Infinit::network_descriptor_get(std::string const& name_,
                                  User const& owner,
                                  bool or_network)
  {
    auto name = qualified_name(name_, owner);
    try
    {
      fs::ifstream f;
      try
      {
        this->_open_read(f, this->_network_path(name, owner), name, "network");
      }
      catch (MissingLocalResource const&)
      {
        this->_open_read(f, this->_network_descriptor_path(name), name, "network");
      }
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<NetworkDescriptor>();
    }
    catch (elle::serialization::Error const&)
    {
      if (or_network)
      {
        try
        {
          return NetworkDescriptor(this->network_get(name_, owner));
        }
        catch (elle::Error const&)
        {}
      }
      throw;
    }
  }

  void
  Infinit::network_save(NetworkDescriptor const& network, bool overwrite) const
  {
    fs::ofstream f;
    this->_open_write(f, this->_network_descriptor_path(network.name),
                      network.name, "network", overwrite);
    save(f, network);
  }

  void
  Infinit::network_save(User const& self,
                        Network const& network, bool overwrite) const
  {
    fs::ofstream f;
    this->_open_write(f, this->_network_path(network.name, self),
                      network.name, "network", overwrite);
    save(f, network);
  }

  auto
  Infinit::passport_get(std::string const& network, std::string const& user)
    -> Passport
  {
    fs::ifstream f;
    this->_open_read(f, this->_passport_path(network, user),
                     elle::sprintf("%s: %s", network, user), "passport");
    return load<Passport>(f);
  }

  auto
  Infinit::passports_get(boost::optional<std::string> network)
    -> std::vector<std::pair<Passport, std::string>>
  {
    auto res = std::vector<std::pair<Passport, std::string>>{};
    fs::path path;
    if (network)
      path = this->_passports_path() / network.get();
    else
      path = this->_passports_path();
    if (!fs::exists(path))
      return res;
    for (auto it = fs::recursive_directory_iterator(path);
         it != fs::recursive_directory_iterator();
         ++it)
      if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
      {
        auto user_name = it->path().filename().string();
        fs::ifstream f;
        this->_open_read(f, it->path(), user_name, "passport");
        auto passport =
          elle::serialization::json::deserialize<model::doughnut::Passport>(f, false);
        res.emplace_back(passport, user_name);
      }
    return res;
  }

  void
  Infinit::passport_save(model::doughnut::Passport const& passport,
                         bool overwrite)
  {
    fs::ofstream f;
    auto users = this->users_get();
    for (auto const& user: users)
    {
      if (user.public_key == passport.user())
      {
        this->_open_write(f,
                          this->_passport_path(passport.network(), user.name),
                          elle::sprintf("%s: %s", passport.network(),
                                        user.name),
                          "passport", overwrite);
        elle::serialization::json::SerializerOut s(f, false, true);
        s.serialize_forward(passport);
        return;
      }
    }
    elle::err("unable to save passport, user not found locally: %s",
              passport.user());
  }

  void
  Infinit::user_save(User const& user,
                     bool overwrite)
  {
    auto path = this->_user_path(user.name);
    fs::ofstream f;
    this->_open_write(f, path, user.name, "user", overwrite);
    save(f, user);
#ifndef INFINIT_WINDOWS
    fs::permissions(path,
                                   fs::remove_perms
                                   | fs::others_all | fs::group_all);
#endif
  }

  User
  Infinit::user_get(std::string const& user, bool beyond_fallback) const
  {
    auto const path = this->_user_path(user);
    try
    {
      fs::ifstream f;
      this->_open_read(f, path, user, "user");
      return load<User>(f);
    }
    catch (MissingLocalResource const& e)
    {
      ELLE_TRACE("%s: unable to load user %s from %s",
                 this, user, this->_user_path(user));
      if (!beyond_fallback)
        throw;
      ELLE_LOG("User %s not found locally, trying on %s", user, beyond());
      auto u = beyond_fetch<User>("user", user);
      return u;
    }
  }

  std::vector<User>
  Infinit::users_get() const
  {
    std::vector<User> res;
    for (fs::recursive_directory_iterator it(this->_users_path());
         it != fs::recursive_directory_iterator();
         ++it)
    {
      if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
      {
        fs::ifstream f;
        this->_open_read(
          f, it->path(), it->path().filename().string(), "user");
        res.push_back(load<User>(f));
      }
    }
    return res;
  }

  fs::path
  Infinit::_user_avatar_path() const
  {
    auto root = xdg_cache_home() / "avatars";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_user_avatar_path(std::string const& name) const
  {
    return this->_user_avatar_path() / name;
  }

  std::unique_ptr<storage::StorageConfig>
  Infinit::storage_get(std::string const& name)
  {
    fs::ifstream f;
    this->_open_read(f, this->_storage_path(name), name, "storage");
    return load<std::unique_ptr<storage::StorageConfig>>(f);
  }

  std::vector<std::unique_ptr<storage::StorageConfig>>
  Infinit::storages_get()
  {
    std::vector<std::unique_ptr<storage::StorageConfig>> res;
    for (fs::recursive_directory_iterator it(this->_storages_path());
         it != fs::recursive_directory_iterator();
         ++it)
    {
      if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
      {
        res.emplace_back(storage_get(it->path().filename().string()));
      }
    }
    return res;
  }

  void
  Infinit::storage_save(std::string const& name,
                        std::unique_ptr<storage::StorageConfig> const& storage)
  {
    fs::ofstream f;
    this->_open_write(f, this->_storage_path(name), name, "storage", false);
    elle::serialization::json::SerializerOut s(f, false, true);
    s.serialize_forward(storage);
  }

  void
  Infinit::storage_remove(std::string const& name)
  {
    auto path = this->_storage_path(name);
    if (!remove(path))
      elle::err("storage \"%s\" does not exist", name);
  }

  std::unordered_map<std::string, std::vector<std::string>>
  Infinit::storage_networks(std::string const& storage_name)
  {
    std::unordered_map<std::string, std::vector<std::string>> res;
    for (auto const& u: this->users_get())
    {
      if (!u.private_key)
        continue;
      for (auto const& n: this->networks_get(u, true))
      {
        auto* d =
          dynamic_cast<infinit::model::doughnut::Configuration*>(n.model.get());
        if (d && d->storage && d->storage->name == storage_name)
        {
          if (res.count(n.name))
          {
            auto& users = res.at(n.name);
            users.emplace_back(u.name);
          }
          else
            res[n.name] = {u.name};
        }
      }
    }
    return res;
  }

  bool
  Infinit::volume_has(std::string const& name)
  {
    fs::ifstream f;
    return exists(this->_volume_path(name));
  }

  Volume
  Infinit::volume_get(std::string const& name)
  {
    fs::ifstream f;
    this->_open_read(f, this->_volume_path(name), name, "volume");
    return load<Volume>(f);
  }

  void
  Infinit::volume_save(Volume const& volume, bool overwrite)
  {
    fs::ofstream f;
    this->_open_write(
      f, this->_volume_path(volume.name), volume.name, "volume", overwrite);
    save(f, volume);
  }

  std::vector<Volume>
  Infinit::volumes_get() const
  {
    std::vector<Volume> res;
    for (fs::recursive_directory_iterator it(this->_volumes_path());
         it != fs::recursive_directory_iterator();
         ++it)
    {
      if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
      {
        fs::ifstream f;
        this->_open_read(
          f, it->path(), it->path().filename().string(), "volume");
        res.push_back(load<Volume>(f));
      }
    }
    return res;
  }

  void
  Infinit::credentials_add(std::string const& name, std::unique_ptr<Credentials> a)
  {
    auto path = this->_credentials_path(name, elle::sprintf("%s", a->uid()));
    fs::ofstream f;
    this->_open_write(f, path, name, "credential");
    save(f, a);
  }

  std::unique_ptr<Credentials>
  Infinit::credentials(std::string const& name, std::string const& identifier) const
  {
    for (auto& account: this->credentials(name))
    {
      if (account->display_name() == identifier ||
          account->uid() == identifier)
      {
        return std::move(account);
      }
    }
    elle::err("no such %s account: %s", name, identifier);
  }

  void
  Infinit::credentials_aws_add(std::unique_ptr<Credentials> a)
  {
    this->credentials_add("aws", std::move(a));
  }

  std::vector<
    std::unique_ptr<
      AWSCredentials,
      std::default_delete<Credentials>
      >
    >
  Infinit::credentials_aws() const
  {
    return this->credentials<AWSCredentials>("aws");
  }

  std::unique_ptr<AWSCredentials, std::default_delete<Credentials>>
  Infinit::credentials_aws(std::string const& uid) const
  {
    auto res = this->credentials("aws", uid);
    return std::dynamic_pointer_cast<AWSCredentials>(res);
  }

  void
  Infinit::credentials_dropbox_add(std::unique_ptr<Credentials> a)
  {
    this->credentials_add("dropbox", std::move(a));
  }

  std::vector<
    std::unique_ptr<
      OAuthCredentials,
      std::default_delete<Credentials>
      >
    >
  Infinit::credentials_dropbox() const
  {
    return this->credentials<OAuthCredentials>("dropbox");
  }

  std::unique_ptr<OAuthCredentials, std::default_delete<Credentials>>
  Infinit::credentials_dropbox(std::string const& uid) const
  {
    auto res = this->credentials("dropbox", uid);
    return std::dynamic_pointer_cast<OAuthCredentials>(res);
  }

  void
  Infinit::credentials_google_add(std::unique_ptr<Credentials> a)
  {
    this->credentials_add("google", std::move(a));
  }

  std::vector<
    std::unique_ptr<
      OAuthCredentials,
      std::default_delete<Credentials>
      >
    >
  Infinit::credentials_google() const
  {
    return this->credentials<OAuthCredentials>("google");
  }

  std::unique_ptr<OAuthCredentials, std::default_delete<Credentials>>
  Infinit::credentials_google(std::string const& uid) const
  {
    auto res = this->credentials("google", uid);
    return std::dynamic_pointer_cast<OAuthCredentials>(res);
  }

  void
  Infinit::credentials_gcs_add(std::unique_ptr<Credentials> a)
  {
    this->credentials_add("gcs", std::move(a));
  }

  std::vector<
    std::unique_ptr<
      OAuthCredentials,
      std::default_delete<Credentials>
      >
    >
  Infinit::credentials_gcs() const
  {
    return this->credentials<OAuthCredentials>("gcs");
  }

  std::unique_ptr<OAuthCredentials, std::default_delete<Credentials>>
  Infinit::credentials_gcs(std::string const& uid) const
  {
    auto res = this->credentials("gcs", uid);
    return std::dynamic_pointer_cast<OAuthCredentials>(res);
  }

  fs::path
  Infinit::_credentials_path() const
  {
    auto root = xdg_data_home() / "credentials";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_credentials_path(std::string const& service) const
  {
    auto root = this->_credentials_path() / service;
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_credentials_path(std::string const& service, std::string const& name) const
  {
    return this->_credentials_path(service) / name;
  }

  fs::path
  Infinit::_network_descriptors_path() const
  {
    auto root = xdg_data_home() / "networks";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_network_descriptor_path(std::string const& name) const
  {
    return this->_network_descriptors_path() / name;
  }

  fs::path
  Infinit::_networks_path(bool create_dir) const
  {
    auto root = xdg_data_home() / "linked_networks";
    if (create_dir)
      create_directories(root);
    return root;
  }

  fs::path
  Infinit::_networks_path(User const& user, bool create_dir) const
  {
    auto root = _networks_path(create_dir) / user.name;
    if (create_dir)
      create_directories(root);
    return root;
  }

  fs::path
  Infinit::_network_path(std::string const& name,
                         User const& user,
                         bool create_dir) const
  {
    auto network_name = this->qualified_name(name, user);
    return this->_networks_path(user, create_dir) / network_name;
  }

  fs::path
  Infinit::_passports_path() const
  {
    auto root = xdg_data_home() / "passports";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_passport_path(std::string const& network, std::string const& user) const
  {
    assert(is_qualified_name(network));
    return this->_passports_path() / network / user;
  }

  fs::path
  Infinit::_storages_path() const
  {
    auto root = xdg_data_home() / "storages";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_storage_path(std::string const& name) const
  {
    return _storages_path() / name;
  }

  fs::path
  Infinit::_users_path() const
  {
    auto root = xdg_data_home() / "users";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_user_path(std::string const& name) const
  {
    return this->_users_path() / name;
  }

  fs::path
  Infinit::_volumes_path() const
  {
    auto root = xdg_data_home() / "volumes";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_volume_path(std::string const& name) const
  {
    return this->_volumes_path() / name;
  }

  void
  Infinit::_open_read(fs::ifstream& f,
                    fs::path const& path,
                    std::string const& name,
                    std::string const& type)
  {
    ELLE_DEBUG("open %s \"%s\" (%s) for reading", type, name, path);
    f.open(path);
    if (!f.good())
      elle::err<MissingLocalResource>("%s \"%s\" does not exist",
                                      type, name);
  }

  void
  Infinit::_open_write(fs::ofstream& f,
                       fs::path const& path,
                       std::string const& name,
                       std::string const& type,
                       bool overwrite,
                       std::ios_base::openmode mode)
  {
    ELLE_DEBUG("open %s \"%s\" (%s) for writing", type, name, path);
    create_directories(path.parent_path());
    if (!overwrite && exists(path))
      elle::err<ResourceAlreadyFetched>("%s \"%s\" already exists",
                                        type, name);
    f.open(path, mode);
    if (!f.good())
      elle::err("unable to open \"%s\" for writing", path);
  }

  fs::path
  Infinit::_drives_path() const
  {
    auto root = xdg_data_home() / "drives";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_drive_path(std::string const& name) const
  {
    return this->_drives_path() / name;
  }

  void
  Infinit::drive_save(Drive const& drive,
                      bool overwrite)
  {
    fs::ofstream f;
    this->_open_write(f, this->_drive_path(drive.name), drive.name, "drive",
                      overwrite);
    save(f, drive);
  }

  Drive
  Infinit::drive_get(std::string const& name)
  {
    fs::ifstream f;
    this->_open_read(f, this->_drive_path(name), name, "drive");
    return load<Drive>(f);
  }

  bool
  Infinit::drive_delete(std::string const& name)
  {
    fs::path drive_path = this->_drive_path(name);
    if (fs::exists(drive_path))
      return fs::remove(drive_path);
    return false;
  }

  fs::path
  Infinit::_drive_icon_path() const
  {
    auto root = xdg_cache_home() / "icons";
    create_directories(root);
    return root;
  }

  fs::path
  Infinit::_drive_icon_path(std::string const& name) const
  {
    return this->_drive_icon_path() / name;
  }

  Drive
  Infinit::drive_fetch(std::string const& name)
  {
    return beyond_fetch<Drive>("drive", name);
  }


  std::vector<std::string>
  Infinit::user_passports_for_network(std::string const& network_name)
  {
    std::vector<std::string> res;
    for (auto const& pair: this->passports_get(network_name))
      res.push_back(pair.second);
    return res;
  }

  std::vector<std::string>
  Infinit::volumes_for_network(std::string const& network_name)
  {
    std::vector<std::string> res;
    for (auto const& volume: this->volumes_get())
    {
      if (volume.network == network_name)
        res.push_back(volume.name);
    }
    return res;
  }

  std::vector<std::string>
  Infinit::drives_for_volume(std::string const& volume_name)
  {
    std::vector<std::string> res;
    for (auto const& drive: this->drives_get())
    {
      if (drive.volume == volume_name)
        res.push_back(drive.name);
    }
    return res;
  }

    /*-------.
    | Beyond |
    `-------*/

  elle::json::Json
  Infinit::beyond_login(std::string const& name,
                        LoginCredentials const& o) const
  {
    reactor::http::Request::Configuration c;
    c.header_add("Content-Type", "application/json");
    reactor::http::Request r(elle::sprintf("%s/users/%s/login", beyond(), name),
                             reactor::http::Method::POST, std::move(c));
    elle::serialization::json::serialize(o, r, false);
    r.finalize();
    if (r.status() != reactor::http::StatusCode::OK)
      infinit::read_error<BeyondError>(r, "login", name);
    return elle::json::read(r);
  }

  static
  std::unique_ptr<reactor::http::Request>
  fetch_data(std::string const& url,
             std::string const& type,
             std::string const& name,
             infinit::Headers const& extra_headers = {},
             Infinit::Reporter report = {})
  {
    infinit::Headers headers;
    for (auto const& header: extra_headers)
      headers[header.first] = header.second;
    reactor::http::Request::Configuration c;
    for (auto const& header: headers)
      c.header_add(header.first, header.second);
    auto r = elle::make_unique<reactor::http::Request>(
      url, reactor::http::Method::GET, std::move(c));
    reactor::wait(*r);
    if (r->status() == reactor::http::StatusCode::OK)
    {
      if (report)
        report(name);
    }
    else if (r->status() == reactor::http::StatusCode::Not_Found)
    {
      infinit::read_error<MissingResource>(*r, type, name);
    }
    else if (r->status() == reactor::http::StatusCode::Gone)
    {
      infinit::read_error<ResourceGone>(*r, type, name);
    }
    else if (r->status() == reactor::http::StatusCode::Forbidden)
    {
      infinit::read_error<ResourceProtected>(*r, type, name);
    }
    else if (r->status() == reactor::http::StatusCode::See_Other
             || r->status() == reactor::http::StatusCode::Temporary_Redirect)
    {
      auto redirection = r->headers().find("Location");
      if (redirection != r->headers().end())
      {
        auto new_url = redirection->second;
        if (!new_url.empty() && new_url != url)
        {
          return fetch_data(new_url, type, name);
        }
      }
    }
    else if (r->status() == reactor::http::StatusCode::Unauthorized)
    {
      elle::err("Unauthorized fetching %s \"%s\", check the system clock",
                type, name);
    }
    else
    {
      elle::err("unexpected HTTP error %s fetching %s",
                r->status(), type);
    }
    return r;
  }

  std::unique_ptr<reactor::http::Request>
  Infinit::beyond_fetch_data(std::string const& where,
                             std::string const& type,
                             std::string const& name,
                             boost::optional<User const&> self,
                             Headers const& extra_headers,
                             Reporter report)
  {
    Headers headers;
    if (self)
    {
      headers = signature_headers(reactor::http::Method::GET, where, self.get());
    }
    for (auto const& header: extra_headers)
      headers[header.first] = header.second;
    return fetch_data(elle::sprintf("%s/%s", beyond(), where),
                      type,
                      name,
                      headers,
                      report);
  }

  elle::json::Json
  Infinit::beyond_fetch_json(std::string const& where,
                             std::string const& type,
                             std::string const& name,
                             boost::optional<User const&> self,
                             Headers const& extra_headers,
                             Reporter report)
  {
    auto r = beyond_fetch_data(
      where, type, name, self, extra_headers, report);
    return elle::json::read(*r);
  }

  void
  Infinit::beyond_delete(std::string const& where,
                         std::string const& type,
                         std::string const& name,
                         User const& self,
                         bool ignore_missing,
                         bool purge,
                         Reporter report)
  {
    reactor::http::Request::Configuration c;
    auto headers = signature_headers(reactor::http::Method::DELETE,
                                     where,
                                     self);
    for (auto const& header: headers)
      c.header_add(header.first, header.second);
    auto url = elle::sprintf("%s/%s", beyond(), where);
    reactor::http::Request::QueryDict query;
    if (purge)
      query["purge"] = "true";
    reactor::http::Request r(url, reactor::http::Method::DELETE, std::move(c));
    r.query_string(query);
    r.finalize();
    reactor::wait(r);
    if (r.status() == reactor::http::StatusCode::OK)
    {
      if (report)
        report(name);
    }
    else if (r.status() == reactor::http::StatusCode::Not_Found)
    {
      if (!ignore_missing)
        read_error<MissingResource>(r, type, name);
    }
    else if (r.status() == reactor::http::StatusCode::See_Other ||
             r.status() == reactor::http::StatusCode::Temporary_Redirect)
    {
      throw Redirected(url);
    }
    else if (r.status() == reactor::http::StatusCode::Unauthorized)
    {
      elle::err("Unauthorized deleting %s \"%s\", check the system clock",
                type, name);
    }
    else if (r.status() == reactor::http::StatusCode::Forbidden)
    {
      elle::err("Forbidden deleting %s \"%s\", "
                "ensure the user has been set using --as or INFINIT_USER",
                type, name);
    }
    else
    {
      ELLE_LOG("HTTP response: %s", r.response());
      elle::err("unexpected HTTP error %s deleting %s \"%s\"",
                r.status(), type, name);
    }
  }

  void
  Infinit::beyond_delete(std::string const& type,
                         std::string const& name,
                         User const& self,
                         bool ignore_missing,
                         bool purge,
                         Reporter report)
  {
    beyond_delete(elle::sprintf("%ss/%s", type, name), type, name, self,
                  ignore_missing, purge, report);
  }

  Infinit::PushResult
  Infinit::beyond_push_data(std::string const& where,
                            std::string const& type,
                            std::string const& name,
                            elle::ConstWeakBuffer const& object,
                            std::string const& content_type,
                            User const& self,
                            bool beyond_error,
                            bool update)
  {
    reactor::http::Request::Configuration c;
    c.header_add("Content-Type", content_type);
    auto headers = signature_headers(
      reactor::http::Method::PUT, where, self, object);
    for (auto const& header: headers)
      c.header_add(header.first, header.second);
    reactor::http::Request r(
      elle::sprintf("%s/%s", beyond(), where),
      reactor::http::Method::PUT, std::move(c));
    r << object.string();
    r.finalize();
    reactor::wait(r);
    if (r.status() == reactor::http::StatusCode::Created)
      return Infinit::PushResult::pushed;
    else if (r.status() == reactor::http::StatusCode::OK)
    {
      if (update)
        return Infinit::PushResult::updated;
      else
        return Infinit::PushResult::alreadyPushed;
    }
    else if (r.status() == reactor::http::StatusCode::Conflict)
    {
      elle::err("%s \"%s\" already exists with a different key", type, name);
    }
    else if (r.status() == reactor::http::StatusCode::Payment_Required)
    {
      elle::err("Pushing %s failed (limit reached):"
                " Please contact sales@infinit.sh.",
                type);
    }
    else if (r.status() == reactor::http::StatusCode::Not_Found)
    {
      if (beyond_error)
        read_error<BeyondError>(r, type, name);
      else
        read_error<MissingResource>(r, type, name);
    }
    else if (r.status() == reactor::http::StatusCode::See_Other ||
             r.status() == reactor::http::StatusCode::Temporary_Redirect)
    {
      throw Redirected(r.url());
    }
    else if (r.status() == reactor::http::StatusCode::Gone)
    {
      read_error<ResourceGone>(r, type, name);
    }
    else if (r.status() == reactor::http::StatusCode::Unauthorized)
    {
      elle::err("Unauthorized pushing %s \"%s\", check the system clock",
                type, name);
    }
    else if (r.status() == reactor::http::StatusCode::Forbidden)
    {
      elle::err("Forbidden pushing %s \"%s\", "
                "ensure the user has been set using --as or INFINIT_USER",
                type, name);
    }
    else
    {
      auto error = [&] {
        try
        {
          auto s = elle::serialization::json::SerializerIn{r, false};
          return s.deserialize<BeyondError>();
        }
        catch (elle::serialization::Error const&)
        {}
        return BeyondError("unknown", "Unknown error");
      }();
      if (error.error() == "user/missing_field/email")
        elle::err("email unspecified (use --email)");
      else if (error.error() == "user/invalid_format/email")
        elle::err("email address is invalid");
      else
        elle::err("unexpected HTTP error %s pushing %s:\n%s",
                  r.status(), type, error);
    }
  }
}
