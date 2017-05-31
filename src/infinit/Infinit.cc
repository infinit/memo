#include <infinit/Infinit.hh>
#include <infinit/utility.hh>

#include <infinit/silo/Filesystem.hh>

ELLE_LOG_COMPONENT("infinit");

namespace infinit
{
  // Deprecated locations for resources.
  namespace deprecated
  {
    // Before 0.8.0, silos were named 'storages', hence, they were located under
    // <home>/<storages>. Because we need to support legacy environments, we
    // need to look for resources under their legacy location.
    bfs::path
    storages_path()
    {
      auto root = xdg_data_home() / "storages";
      create_directories(root);
      return root;
    }
    // See storages_path.
    bfs::path
    silo_path(std::string const& name)
    {
      return storages_path() / name;
    }
  }

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

  bool
  Infinit::_delete(bfs::path const& path,
                   std::string const& type,
                   std::string const& name)
  {
    boost::system::error_code erc;
    if (!bfs::exists(path))
    {
      ELLE_TRACE("Nothing to delete for %s \"%s\" (%s)", type, name, path);
      return false;
    }
    bfs::remove(path, erc);
    if (erc)
      elle::err("Unable to delete %s \"%s\": %s", type, name, erc.message());
    this->report_local_action()("deleted", type, name);
    return true;
  }

  bool
  Infinit::_delete_all(bfs::path const& path,
                       std::string const& type,
                       std::string const& name)
  {
    boost::system::error_code erc;
    if (!bfs::exists(path))
    {
      ELLE_TRACE("Nothing to delete for %s \"%s\" (%s)", type, name, path);
      return false;
    }
    bfs::remove_all(path, erc);
    if (erc)
      elle::err("Unable to delete %s \"%s\": %s", type, name, erc.message());
    this->report_local_action()("deleted", type, name);
    return true;
  }

  Network
  Infinit::network_get(std::string const& name_,
                       User const& user,
                       bool require_model)
  {
    auto name = qualified_name(name_, user);
    bfs::ifstream f;
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
      bfs::ifstream temp_f;
      this->_open_read(
        temp_f, this->_network_descriptor_path(name), name, "network");
      auto temp_net =
        elle::serialization::json::deserialize<Network>(temp_f, false);
      NetworkDescriptor desc(std::move(temp_net));
      auto old_path = this->_network_descriptor_path(res.name);
      auto path = this->_network_path(res.name, user);
      create_directories(path.parent_path());
      bfs::rename(old_path, path);
      this->network_save(desc);
    }
    return res;
  }

  std::vector<Network>
  Infinit::networks_get(
    boost::optional<User> self,
    bool require_linked) const
  {
    auto res = std::vector<Network>{};
    auto extract =
      [&] (bfs::path const& path, bool move) {
      for (auto const& p: bfs::recursive_directory_iterator(path))
        if (is_visible_file(p))
        {
          bfs::ifstream f;
          this->_open_read(
            f, p.path(), "network", p.path().filename().string());
          auto network =
            elle::serialization::json::deserialize<Network>(f, false);
          if (require_linked && !network.model)
            continue;
          if (move && network.model && self && network.user_linked(*self))
          {
            bfs::ifstream temp_f;
            this->_open_read(
              temp_f, p.path(), "network", p.path().filename().string());
            auto temp_net =
              elle::serialization::json::deserialize<Network>(temp_f, false);
            auto desc = NetworkDescriptor(std::move(temp_net));
            auto path = this->_network_path(network.name, *self);
            create_directories(path.parent_path());
            bfs::rename(p.path(), path);
            this->network_save(desc);
          }
          // Ignore duplicates.
          if (std::find_if(res.begin(), res.end(),
                           [&network] (Network const& n) {
                             return n.name == network.name;
                           }) == res.end())
            res.emplace_back(std::move(network));
        }
    };
    if (self)
      // Start by linked networks first.
      extract(this->_networks_path(*self), false);
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
                       return !bfs::exists(this->_network_path(name, u, false));
                     }),
      res.end());
    return res;
  }

  void
  Infinit::network_unlink(std::string const& name_,
                          User const& user)
  {
    auto name = qualified_name(name_, user);
    auto network = this->network_get(name, user, true);
    auto path = this->_network_path(network.name, user);
    // XXX Should check async cache to make sure that it's empty.
    bfs::remove_all(network.cache_dir(user).parent_path());
    if (bfs::exists(path))
    {
      boost::system::error_code erc;
      bfs::remove(path, erc);
      if (erc)
      {
        ELLE_WARN("Unable to unlink network \"%s\": %s",
                  network.name, erc.message());
      }
      else
      {
        this->report_local_action()("unlinked", "network",
                                    network.name);
      }
    }
  }

  bool
  Infinit::network_delete(std::string const& name_,
                          User const& user,
                          bool unlink)
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
    // XXX: Why do we need to pacify the exception to a WARN ?
    auto pacify_exception = [] (std::function<bool ()> const& action) -> bool
      {
        try
        {
          return action();
        }
        catch (elle::Error const&)
        {
          ELLE_WARN("%s", elle::exception_string());
        }
        return false;
      };
    for (auto const& u: linked_users)
    {
      pacify_exception([&] {
        return this->_delete(
          this->_network_path(name, u), "link for network",
          name);
      });
      pacify_exception([&] {
        return this->_delete_all(network.cache_dir(u).parent_path(),
                                 "cache for network", name);
      });
    }
    return pacify_exception([&] {
      return this->_delete(this->_network_descriptor_path(name),
                           "network", name);
    });
  }

  std::vector<Drive>
  Infinit::drives_get() const
  {
    auto res = std::vector<Drive>{};
    for (auto const& p
           : bfs::recursive_directory_iterator(this->_drives_path()))
      if (is_visible_file(p))
      {
        bfs::ifstream f;
        this->_open_read(
          f, p.path(), p.path().filename().string(), "drive");
        res.emplace_back(load<Drive>(f));
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
      bfs::ifstream f;
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
    bfs::ofstream f;
    bool existed = this->_open_write(
      f, this->_network_descriptor_path(network.name),
      network.name, "network", overwrite);
    save(f, network);
    this->report_local_action()(
      existed ? "updated" : "saved", "network descriptor",
      network.name);
  }

  void
  Infinit::network_save(User const& self,
                        Network const& network, bool overwrite) const
  {
    bfs::ofstream f;
    bool existed = this->_open_write(f, this->_network_path(network.name, self),
                                     network.name, "network", overwrite);
    this->report_local_action()(existed ? "updated linked" : "linked", "network",
                                network.name);
    save(f, network);
  }

  auto
  Infinit::passport_get(std::string const& network, std::string const& user)
    -> Passport
  {
    bfs::ifstream f;
    this->_open_read(f, this->_passport_path(network, user),
                     elle::sprintf("%s: %s", network, user), "passport");
    return load<Passport>(f);
  }

  bool
  Infinit::passport_delete(Passport const& passport)
  {
    for (auto const& user: this->users_get())
    {
      if (user.public_key == passport.user())
      {
        if (this->passport_delete(passport.network(), user.name))
          return true;
      }
    }
    return false;
  }

  bool
  Infinit::passport_delete(std::string const& network_name,
                           std::string const& user_name)
  {
    ELLE_ASSERT(this->is_qualified_name(network_name));
    return this->_delete(this->_passport_path(network_name, user_name),
                         "passport",
                         elle::sprintf("%s: %s", network_name, user_name));
  }

  auto
  Infinit::passports_get(boost::optional<std::string> network)
    -> std::vector<std::pair<Passport, std::string>>
  {
    auto res = std::vector<std::pair<Passport, std::string>>{};
    bfs::path path;
    if (network)
      path = this->_passports_path() / network.get();
    else
      path = this->_passports_path();
    if (!bfs::exists(path))
      return res;
    for (auto const& p: bfs::recursive_directory_iterator(path))
      if (is_visible_file(p))
      {
        auto user_name = p.path().filename().string();
        bfs::ifstream f;
        this->_open_read(f, p.path(), user_name, "passport");
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
    bfs::ofstream f;
    auto users = this->users_get();
    for (auto const& user: users)
    {
      if (user.public_key == passport.user())
      {
        bool existed = this->_open_write(
          f,
          this->_passport_path(passport.network(), user.name),
          elle::sprintf("%s: %s", passport.network(),
                        user.name),
          "passport", overwrite);
        elle::serialization::json::SerializerOut s(f, false, true);
        s.serialize_forward(passport);
        this->report_local_action()(
          existed ? "updated" : "saved" , "passport",
          elle::sprintf("%s: %s", passport.network(), user.name));
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
    bfs::ofstream f;
    bool existed = this->_open_write(f, path, user.name, "user", overwrite);
    save(f, user);
    this->report_local_action()(existed ? "updated" : "saved", "user",
                                user.name);
#ifndef INFINIT_WINDOWS
    bfs::permissions(path,
                     bfs::remove_perms | bfs::others_all | bfs::group_all);
#endif
  }

  bool
  Infinit::user_delete(User const& user)
  {
    return this->_delete(this->_user_path(user.name), "user", user.name);
  }

  User
  Infinit::user_get(std::string const& user, bool beyond_fallback) const
  {
    auto const path = this->_user_path(user);
    try
    {
      bfs::ifstream f;
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
    auto res = std::vector<User>{};
    for (auto const& p: bfs::recursive_directory_iterator(this->_users_path()))
      if (is_visible_file(p))
      {
        bfs::ifstream f;
        this->_open_read(f, p.path(), p.path().filename().string(), "user");
        res.emplace_back(load<User>(f));
      }
    return res;
  }

  bfs::path
  Infinit::_avatars_path() const
  {
    auto root = xdg_cache_home() / "avatars";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_avatar_path(std::string const& name) const
  {
    return this->_avatars_path() / name;
  }

  bool
  Infinit::avatar_delete(User const& user)
  {
    return this->_delete(this->_avatar_path(user.name), "avatar", user.name);
  }

  Infinit::SiloConfigPtr
  Infinit::silo_get(std::string const& name)
  {
    bfs::ifstream f;
    // Search for the silo configuration in the new location.
    try
    {
      this->_open_read(f, this->_silo_path(name), name, "silo");
    }
    // Fallback in the deprecated silo location.
    catch (MissingLocalResource const&)
    {
      this->_open_read(f, deprecated::storages_path() / name, name, "storage");
    }
    return load<SiloConfigPtr>(f);
  }

  Infinit::Silos
  Infinit::silos_get()
  {
    auto res = Infinit::Silos{};
    auto get = [&] (bfs::path const& path)
      {
        for (auto const& p: bfs::recursive_directory_iterator(path))
          if (is_visible_file(p))
            res.emplace(silo_get(p.path().filename().string()));
      };
    // Search for silos configurations in the new location.
    get(this->_silos_path());
    // Search for silos in the deprecated directory. Because Silos is the
    // flat_set, duplicates are ignored.
    // N.B. It shouldn't be any, except if the user did a manual copy.
    get(deprecated::storages_path());
    return res;
  }

  void
  Infinit::silo_save(std::string const& name,
                     SiloConfigPtr const& silo)
  {
    bfs::ofstream f;
    bool existed = this->_open_write(f, this->_silo_path(name), name,
                                     "silo", false);
    elle::serialization::json::SerializerOut s(f, false, true);
    s.serialize_forward(silo);
    // Try to delete the old silo configuration so the system cleans up by it
    // self.
    existed |= this->_delete(deprecated::silo_path(name), "silo", name);
    this->report_local_action()(existed ? "updated" : "saved", "silo", name);
  }

  bool
  Infinit::silo_delete(SiloConfigPtr const& silo,
                       bool clear)
  {
    auto const& name = silo->name;
    if (clear)
    {
      if (auto fs_silo =
          dynamic_cast<infinit::silo::FilesystemSiloConfig*>(silo.get()))
        this->_delete_all(fs_silo->path, "silo content", name);
      else
        elle::err("only filesystem silos can be cleared");
    }
    auto del = [this] (bfs::path const& p,
                       std::string const& name)
      {
        return this->_delete(p, "silo", name);
      };
    // Try to delete the silo configuration from both possible location.
    auto d = std::make_pair(
      del(deprecated::silo_path(name), name),
      del(this->_silo_path(name), name)
    );
    return d.first || d.second;
  }

  std::unordered_map<std::string, std::vector<std::string>>
  Infinit::silo_networks(std::string const& silo_name)
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
        if (d && d->storage && d->storage->name == silo_name)
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
    bfs::ifstream f;
    return exists(this->_volume_path(name));
  }

  Volume
  Infinit::volume_get(std::string const& name)
  {
    bfs::ifstream f;
    this->_open_read(f, this->_volume_path(name), name, "volume");
    return load<Volume>(f);
  }

  void
  Infinit::volume_save(Volume const& volume, bool overwrite)
  {
    bfs::ofstream f;
    bool existed = this->_open_write(
      f, this->_volume_path(volume.name), volume.name, "volume", overwrite);
    save(f, volume);
    this->report_local_action()(existed ? "updated" : "saved", "volume",
                                volume.name);
  }

  bool
  Infinit::volume_delete(Volume const& volume)
  {
    auto deleted =
      this->_delete(this->_volume_path(volume.name), "volume", volume.name);
    this->_delete_all(volume.root_block_cache_dir(), "root block for volume",
                      volume.name);
    return deleted;
  }

  std::vector<Volume>
  Infinit::volumes_get() const
  {
    auto res = std::vector<Volume>{};
    for (auto const& p
           : bfs::recursive_directory_iterator(this->_volumes_path()))
      if (is_visible_file(p))
      {
        bfs::ifstream f;
        this->_open_read(f, p.path(), p.path().filename().string(), "volume");
        res.emplace_back(load<Volume>(f));
      }
    return res;
  }

  void
  Infinit::credentials_add(std::string const& name, std::unique_ptr<Credentials> a)
  {
    auto path = this->_credentials_path(name, elle::sprintf("%s", a->uid()));
    bfs::ofstream f;
    bool existed = this->_open_write(f, path, name, "credential", true);
    save(f, a);
    this->report_local_action()(
      existed ? "updated" : "saved", elle::sprintf("%s credentials", name),
      a->display_name());
  }

  bool
  Infinit::credentials_delete(std::string const& type,
                              std::string const& account_name)
  {
    return this->_delete(this->_credentials_path(type, account_name),
                         elle::sprintf("%s credentials", type),
                         account_name);
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

  bfs::path
  Infinit::_credentials_path() const
  {
    auto root = xdg_data_home() / "credentials";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_credentials_path(std::string const& service) const
  {
    auto root = this->_credentials_path() / service;
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_credentials_path(std::string const& service, std::string const& name) const
  {
    return this->_credentials_path(service) / name;
  }

  bfs::path
  Infinit::_network_descriptors_path() const
  {
    auto root = xdg_data_home() / "networks";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_network_descriptor_path(std::string const& name) const
  {
    return this->_network_descriptors_path() / name;
  }

  bfs::path
  Infinit::_networks_path(bool create_dir) const
  {
    auto root = xdg_data_home() / "linked_networks";
    if (create_dir)
      create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_networks_path(User const& user, bool create_dir) const
  {
    auto root = _networks_path(create_dir) / user.name;
    if (create_dir)
      create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_network_path(std::string const& name,
                         User const& user,
                         bool create_dir) const
  {
    auto network_name = this->qualified_name(name, user);
    return this->_networks_path(user, create_dir) / network_name;
  }

  bfs::path
  Infinit::_passports_path() const
  {
    auto root = xdg_data_home() / "passports";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_passport_path(std::string const& network, std::string const& user) const
  {
    assert(is_qualified_name(network));
    return this->_passports_path() / network / user;
  }

  bfs::path
  Infinit::_silos_path() const
  {
    auto root = xdg_data_home() / "silos";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_silo_path(std::string const& name) const
  {
    return _silos_path() / name;
  }

  bfs::path
  Infinit::_users_path() const
  {
    auto root = xdg_data_home() / "users";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_user_path(std::string const& name) const
  {
    return this->_users_path() / name;
  }

  bfs::path
  Infinit::_volumes_path() const
  {
    auto root = xdg_data_home() / "volumes";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_volume_path(std::string const& name) const
  {
    return this->_volumes_path() / name;
  }

  void
  Infinit::_open_read(bfs::ifstream& f,
                    bfs::path const& path,
                    std::string const& name,
                    std::string const& type)
  {
    ELLE_DEBUG("open %s \"%s\" (%s) for reading", type, name, path);
    f.open(path);
    if (!f.good())
      elle::err<MissingLocalResource>("%s \"%s\" does not exist",
                                      type, name);
  }

  bool
  Infinit::_open_write(bfs::ofstream& f,
                       bfs::path const& path,
                       std::string const& name,
                       std::string const& type,
                       bool overwrite,
                       std::ios_base::openmode mode)
  {
    ELLE_DEBUG("open %s \"%s\" (%s) for writing", type, name, path);
    create_directories(path.parent_path());
    auto exists = bfs::exists(path);
    if (!overwrite && exists)
      elle::err<ResourceAlreadyFetched>("%s \"%s\" already exists",
                                        type, name);
    f.open(path, mode);
    if (!f.good())
      elle::err("unable to open \"%s\" for writing", path);
    return exists;
  }

  bfs::path
  Infinit::_drives_path() const
  {
    auto root = xdg_data_home() / "drives";
    create_directories(root);
    return root;
  }

  bfs::path
  Infinit::_drive_path(std::string const& name) const
  {
    return this->_drives_path() / name;
  }

  void
  Infinit::drive_save(Drive const& drive,
                      bool overwrite)
  {
    bfs::ofstream f;
    bool existed = this->_open_write(
      f, this->_drive_path(drive.name), drive.name, "drive", overwrite);
    save(f, drive);
    this->report_local_action()(existed ? "updated" : "saved", "drive",
                                drive.name);
  }

  Drive
  Infinit::drive_get(std::string const& name)
  {
    bfs::ifstream f;
    this->_open_read(f, this->_drive_path(name), name, "drive");
    return load<Drive>(f);
  }

  bool
  Infinit::drive_delete(Drive const& drive)
  {
    return this->_delete(this->_drive_path(drive.name), "drive", drive.name);
  }

  bfs::path
  Infinit::_drive_icon_path() const
  {
    auto res = xdg_cache_home() / "icons";
    create_directories(res);
    return res;
  }

  bfs::path
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
    return elle::make_vector(this->passports_get(network_name),
                             [](auto const& p)
                             {
                               return p.second;
                             });
  }

  std::vector<Volume>
  Infinit::volumes_for_network(std::string const& network_name)
  {
    std::vector<Volume> res;
    for (auto const& volume: this->volumes_get())
    {
      if (volume.network == network_name)
        res.push_back(volume);
    }
    return res;
  }

  std::vector<Drive>
  Infinit::drives_for_volume(std::string const& volume_name)
  {
    std::vector<Drive> res;
    for (auto const& drive: this->drives_get())
    {
      if (drive.volume == volume_name)
        res.push_back(drive);
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
    elle::reactor::http::Request::Configuration c;
    c.header_add("Content-Type", "application/json");
    auto r = elle::reactor::http::Request
      (elle::sprintf("%s/users/%s/login", beyond(), name),
       elle::reactor::http::Method::POST, std::move(c));
    elle::serialization::json::serialize(o, r, false);
    r.finalize();
    if (r.status() != elle::reactor::http::StatusCode::OK)
      infinit::read_error<BeyondError>(r, "login", name);
    return elle::json::read(r);
  }

  static
  std::unique_ptr<elle::reactor::http::Request>
  fetch_data(std::string const& url,
             std::string const& type,
             std::string const& name,
             infinit::Headers const& extra_headers = {})
  {
    auto headers = extra_headers;
    elle::reactor::http::Request::Configuration c;
    c.header_add(headers);
    auto r = std::make_unique<elle::reactor::http::Request>(
      url, elle::reactor::http::Method::GET, std::move(c));
    elle::reactor::wait(*r);
    if (r->status() == elle::reactor::http::StatusCode::OK)
    {
    }
    else if (r->status() == elle::reactor::http::StatusCode::Not_Found)
    {
      infinit::read_error<MissingResource>(*r, type, name);
    }
    else if (r->status() == elle::reactor::http::StatusCode::Gone)
    {
      infinit::read_error<ResourceGone>(*r, type, name);
    }
    else if (r->status() == elle::reactor::http::StatusCode::Forbidden)
    {
      infinit::read_error<ResourceProtected>(*r, type, name);
    }
    else if (r->status() == elle::reactor::http::StatusCode::See_Other
             || r->status() == elle::reactor::http::StatusCode::Temporary_Redirect)
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
    else if (r->status() == elle::reactor::http::StatusCode::Unauthorized)
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

  std::unique_ptr<elle::reactor::http::Request>
  Infinit::beyond_fetch_data(std::string const& where,
                             std::string const& type,
                             std::string const& name,
                             boost::optional<User const&> self,
                             Headers const& extra_headers) const
  {
    auto headers =
      self
      ? signature_headers(elle::reactor::http::Method::GET, where, self.get())
      : Headers{};
    headers.insert(extra_headers.begin(), extra_headers.end());
    return fetch_data(elle::sprintf("%s/%s", beyond(), where),
                      type,
                      name,
                      headers);
  }

  elle::json::Json
  Infinit::beyond_fetch_json(std::string const& where,
                             std::string const& type,
                             std::string const& name,
                             boost::optional<User const&> self,
                             Headers const& extra_headers) const
  {
    auto r = beyond_fetch_data(where, type, name, self, extra_headers);
    return elle::json::read(*r);
  }

  bool
  Infinit::beyond_delete(std::string const& where,
                         std::string const& type,
                         std::string const& name,
                         User const& self,
                         bool ignore_missing,
                         bool purge) const
  {
    auto c = elle::reactor::http::Request::Configuration{};
    c.header_add(signature_headers(elle::reactor::http::Method::DELETE,
                                   where,
                                   self));
    auto url = elle::sprintf("%s/%s", beyond(), where);
    elle::reactor::http::Request::QueryDict query;
    if (purge)
      query["purge"] = "true";
    elle::reactor::http::Request r(url, elle::reactor::http::Method::DELETE, std::move(c));
    r.query_string(query);
    r.finalize();
    elle::reactor::wait(r);
    if (r.status() == elle::reactor::http::StatusCode::OK)
    {
      this->report_remote_action()("deleted", type, name);
      return true;
    }
    else if (r.status() == elle::reactor::http::StatusCode::Not_Found)
    {
      if (!ignore_missing)
        read_error<MissingResource>(r, type, name);
      return false;
    }
    else if (r.status() == elle::reactor::http::StatusCode::See_Other ||
             r.status() == elle::reactor::http::StatusCode::Temporary_Redirect)
    {
      throw Redirected(url);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Unauthorized)
    {
      elle::err("Unauthorized deleting %s \"%s\", check the system clock",
                type, name);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Forbidden)
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

  bool
  Infinit::beyond_delete(std::string const& type,
                         std::string const& name,
                         User const& self,
                         bool ignore_missing,
                         bool purge) const
  {
    return beyond_delete(elle::sprintf("%ss/%s", type, name), type, name, self,
                         ignore_missing, purge);
  }

  Infinit::PushResult
  Infinit::beyond_push_data(std::string const& where,
                            std::string const& type,
                            std::string const& name,
                            elle::ConstWeakBuffer const& object,
                            std::string const& content_type,
                            User const& self,
                            bool beyond_error,
                            bool update) const
  {
    elle::reactor::http::Request::Configuration c;
    c.header_add("Content-Type", content_type);
    c.header_add(signature_headers(
      elle::reactor::http::Method::PUT, where, self, object));
    auto r = elle::reactor::http::Request(
      elle::sprintf("%s/%s", beyond(), where),
      elle::reactor::http::Method::PUT, std::move(c));
    r << object.string();
    r.finalize();
    elle::reactor::wait(r);
    if (r.status() == elle::reactor::http::StatusCode::Created)
    {
      this->report_remote_action()("created", type, name);
      return Infinit::PushResult::pushed;
    }
    else if (r.status() == elle::reactor::http::StatusCode::OK)
    {
      if (update)
      {
        this->report_remote_action()("updated", type, name);
        return Infinit::PushResult::updated;
      }
      else
      {
        this->report_remote_action()("already updated", type, name);
        return Infinit::PushResult::alreadyPushed;
      }
    }
    else if (r.status() == elle::reactor::http::StatusCode::Conflict)
    {
      elle::err("%s \"%s\" already exists with a different key", type, name);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Payment_Required)
    {
      elle::err("Pushing %s failed (limit reached):"
                " Please contact sales@infinit.sh.",
                type);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Not_Found)
    {
      if (beyond_error)
        read_error<BeyondError>(r, type, name);
      else
        read_error<MissingResource>(r, type, name);
    }
    else if (r.status() == elle::reactor::http::StatusCode::See_Other ||
             r.status() == elle::reactor::http::StatusCode::Temporary_Redirect)
    {
      throw Redirected(r.url());
    }
    else if (r.status() == elle::reactor::http::StatusCode::Gone)
    {
      read_error<ResourceGone>(r, type, name);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Unauthorized)
    {
      elle::err("Unauthorized pushing %s \"%s\", check the system clock",
                type, name);
    }
    else if (r.status() == elle::reactor::http::StatusCode::Forbidden)
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
