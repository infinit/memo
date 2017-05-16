#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef INFINIT_LINUX
# include <mntent.h>
#endif

#ifdef INFINIT_MACOSX
# include <sys/param.h>
# include <sys/mount.h>
#endif

#include <elle/log.hh>
#include <elle/system/PIDFile.hh>
#include <elle/system/Process.hh>
#include <elle/system/self-path.hh>
#include <elle/system/unistd.hh>
#include <elle/make-vector.hh>

#include <infinit/Infinit.hh>
#include <infinit/cli/MountManager.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/silo/Filesystem.hh>

ELLE_LOG_COMPONENT("cli.MountManager");

namespace infinit
{
  using Passport = infinit::model::doughnut::Passport;

  namespace cli
  {
    namespace
    {
      boost::optional<std::string>
      optional(elle::json::Object const& options, std::string const& name)
      {
        auto it = options.find(name);
        if (it == options.end())
          return {};
        else
          return boost::any_cast<std::string>(it->second);
      }
    }

    std::pair<std::string, infinit::User>
    split(infinit::Infinit& ifnt,
          std::string const& name)
    {
      auto p = name.find('/');
      if (p == name.npos)
        elle::err("Malformed qualified name");
      return {name.substr(p+1), ifnt.user_get(name.substr(0, p))};
    }

    void link_network(infinit::Infinit& ifnt,
                      std::string const& name,
                      elle::json::Object const& options = elle::json::Object{})
    {
      auto cname = split(ifnt, name);
      auto desc = ifnt.network_descriptor_get(cname.first, cname.second, false);
      auto users = ifnt.users_get();
      auto passport = boost::optional<infinit::Passport>{};
      auto user = boost::optional<infinit::User>{};
      ELLE_TRACE("checking if any user is owner");
      for (auto const& u: users)
        if (u.public_key == desc.owner && u.private_key)
        {
          passport.emplace(u.public_key, desc.name,
            elle::cryptography::rsa::KeyPair(u.public_key,
                                                u.private_key.get()));
          user.emplace(u);
          break;
        }
      if (!passport)
      {
        ELLE_TRACE("Trying to acquire passport");
        for (auto const& u: users)
        {
          try
          {
            passport.emplace(ifnt.passport_get(name, u.name));
            user.emplace(u);
            break;
          }
          catch (infinit::MissingLocalResource const&)
          {
            try
            {
              passport.emplace
                (ifnt.template beyond_fetch<infinit::Passport>(elle::sprintf(
                "networks/%s/passports/%s", name, u.name),
                  "passport for",
                  name,
                  u));
              user.emplace(u);
              break;
            }
            catch (elle::Error const&)
            {}
          }
        }
      }
      if (!passport)
        elle::err("Failed to acquire passport.");
      ELLE_TRACE("Passport found for user %s", user->name);

      auto silo_config = [&] () -> std::unique_ptr<infinit::storage::StorageConfig> {
        auto silodesc = optional(options, "silo");
        if (silodesc && silodesc->empty())
        {
          auto siloname = boost::replace_all_copy(name + "_silo", "/", "_");
          ELLE_LOG("Creating local silo %s", siloname);
          auto path = infinit::xdg_data_home() / "blocks" / siloname;
          return
            std::make_unique<infinit::storage::FilesystemStorageConfig>(
              siloname, path.string(), boost::none, boost::none);
        }
        else if (silodesc)
        {
          try
          {
            return ifnt.silo_get(*silodesc);
          }
          catch (infinit::MissingLocalResource const&)
          {
            elle::err("silo specification for new silo not implemented");
          }
        }
        else
          return nullptr;
      }();

      auto network = infinit::Network(
        desc.name,
        std::make_unique<infinit::model::doughnut::Configuration>(
          infinit::model::Address::random(0), // FIXME
          std::move(desc.consensus),
          std::move(desc.overlay),
          std::move(silo_config),
          user->keypair(),
          std::make_shared<elle::cryptography::rsa::PublicKey>(desc.owner),
          std::move(*passport),
          user->name,
          boost::optional<int>(),
          desc.version,
          desc.admin_keys,
          std::vector<infinit::model::Endpoints>(),
          desc.tcp_heartbeat,
          std::move(desc.encrypt_options)),
        boost::none);
      ifnt.network_save(*user, network, true);
      ifnt.network_save(std::move(network), true);
    }

    void
    acquire_network(infinit::Infinit& ifnt,
                    std::string const& name)
    {
      auto desc = ifnt.beyond_fetch<infinit::NetworkDescriptor>("network", name);
      ifnt.network_save(desc);
      try
      {
        auto nname = split(ifnt, name);
        auto net = ifnt.network_get(nname.first, nname.second, true);
      }
      catch (elle::Error const&)
      {
        link_network(ifnt, name);
      }
    }

    void
    acquire_volume(infinit::Infinit& ifnt,
                   std::string const& name)
    {
      auto desc = ifnt.beyond_fetch<infinit::Volume>("volume", name);
      ifnt.volume_save(desc, true);
      try
      {
        auto nname = split(ifnt, desc.network);
        auto net = ifnt.network_get(nname.first, nname.second, true);
      }
      catch (infinit::MissingLocalResource const&)
      {
        acquire_network(ifnt, desc.network);
      }
      catch (elle::Error const&)
      {
        link_network(ifnt, desc.network);
      }
    }

    MountManager::~MountManager()
    {
      while (!this->_mounts.empty())
        this->stop(this->_mounts.begin()->first);
    }

    void
    MountManager::acquire_volumes()
    {
      if (this->_fetch)
        for (auto const& u: _ifnt.users_get())
          if (u.private_key)
            try
            {
              auto res = _ifnt.template beyond_fetch<
                std::unordered_map<std::string, std::vector<infinit::Volume>>>(
                  elle::sprintf("users/%s/volumes", u.name),
                  "volumes for user",
                  u.name,
                  u);
              for (auto const& volume: res["volumes"])
              {
                try
                {
                  acquire_volume(_ifnt, volume.name);
                }
                catch (infinit::ResourceAlreadyFetched const& error)
                {
                }
                catch (elle::Error const& e)
                {
                  ELLE_WARN("failed to acquire %s: %s", volume.name, e);
                }
              }
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("failed to acquire volumes from beyond: %s", e);
            }
      if (this->_default_network)
      {
        ELLE_TRACE_SCOPE("%s: list volumes from network %s",
                         this, this->_default_network.get());
        auto owner = _ifnt.user_get(this->default_user());
        auto net = _ifnt.network_get(this->_default_network.get(), owner, true);
        auto process = [&]
        {
          static const auto root = elle::system::self_path().parent_path();
          auto args = std::vector<std::string>{
            (root / "infinit").string(),
            "volume",
            "fetch",
            "--network",
            net.name,
            "--service",
            "--as",
            this->default_user(),
          };
          infinit::MountOptions mo;
          if (this->_fetch)
            mo.fetch = true;
          if (this->_push)
            mo.push = true;
          auto env = elle::os::Environ{};
          mo.to_commandline(args, env);
          return std::make_unique<elle::system::Process>(args, true);
        }();
        process->wait();
      }
    }

    std::vector<infinit::descriptor::BaseDescriptor::Name>
    MountManager::list()
    {
      this->acquire_volumes();
      return elle::make_vector(_ifnt.volumes_get(),
                               [](auto&& v) { return v.name; });
    }

    std::string
    MountManager::mountpoint(std::string const& name, bool raw)
    {
      auto it = _mounts.find(name);
      if (it == _mounts.end())
        elle::err("not mounted: %s", name);
      if (!it->second.options.mountpoint)
        elle::err("running without mountpoint: %s", name);
      std::string pre = it->second.options.mountpoint.get();
      if (!raw && !this->_mount_substitute.empty())
      {
        auto sep = this->_mount_substitute.find(":");
        if (sep == std::string::npos)
          pre = this->_mount_substitute + pre;
        else
        {
          std::string search = this->_mount_substitute.substr(0, sep);
          std::string repl = this->_mount_substitute.substr(sep+1);
          auto pos = pre.find(search);
          if (pos != pre.npos)
          {
            pre = pre.substr(0, pos) + repl + pre.substr(pos + search.size());
          }
        }
      }
      ELLE_TRACE("replying with mountpoint %s", pre);
      return pre;
    }

    bool
    MountManager::exists(std::string const& name)
    {
      try
      {
        auto volume = _ifnt.volume_get(name);
        return true;
      }
      catch (elle::Error const& e)
      {
        return false;
      }
    }

    bool
    is_mounted(std::string const& path)
    {
#if defined INFINIT_LINUX
      FILE* mtab = setmntent("/etc/mtab", "r");
      struct mntent mnt;
      char strings[4096];
      bool found = false;
      while (getmntent_r(mtab, &mnt, strings, sizeof strings))
        if (std::string(mnt.mnt_dir) == path)
        {
          found = true;
          break;
        }
      endmntent(mtab);
      return found;
#elif defined INFINIT_WINDOWS
      // We mount as drive letters under windows
      return bfs::exists(path);
#elif defined INFINIT_MACOSX
      struct statfs sfs;
      int res = statfs(path.c_str(), &sfs);
      if (res)
        return false;
      return bfs::path(path) == bfs::path(sfs.f_mntonname);
#else
      elle::err("is_mounted is not implemented");
#endif
    }

    void
    MountManager::start(std::string const& name,
                        infinit::MountOptions opts,
                        bool force_mount,
                        bool wait_for_mount,
                        std::vector<std::string> const& extra_args)
    {
      infinit::Volume volume = [&]
      {
        try
        {
          return _ifnt.volume_get(name);
        }
        catch (infinit::MissingLocalResource const&)
        {
          acquire_volume(_ifnt, name);
          return _ifnt.volume_get(name);
        }
      }();
      if (this->_mounts.count(volume.name))
        elle::err("already mounted: %s", volume.name);
      try
      {
        infinit::User u = [&] {
          if (volume.mount_options.as)
            return _ifnt.user_get(*volume.mount_options.as);
          else
            return _ifnt.user_get(volume.network.substr(0, volume.network.find('/')));
        }();
        auto net = _ifnt.network_get(volume.network, u, true);
      }
      catch (infinit::MissingLocalResource const&)
      {
        acquire_network(_ifnt, volume.network);
      }
      catch (elle::Error const&)
      {
        link_network(_ifnt, volume.network);
      }
      volume.mount_options.merge(opts);
      if (!volume.mount_options.fuse_options)
        volume.mount_options.fuse_options = std::vector<std::string>{"allow_root"};
      else
        volume.mount_options.fuse_options->push_back("allow_root");
      Mount m{nullptr, volume.mount_options};
      auto mount_prefix = boost::replace_all_copy(name + "-", "/", "_");
      if (force_mount && !m.options.mountpoint)
      {
        auto username = elle::system::username();
        boost::system::error_code erc;
        bfs::permissions(this->_mount_root,
                         bfs::remove_perms
                         | bfs::others_read
                         | bfs::others_exe
                         | bfs::others_write
                         );
        m.options.mountpoint =
        (this->_mount_root /
          (mount_prefix + bfs::unique_path().string())).string();
      }
      if (this->_fetch)
        m.options.fetch = true;
      if (this->_push)
        m.options.push = true;
      static const auto root = elle::system::self_path().parent_path();
      auto args = std::vector<std::string>{
        (root / "infinit").string(),
        "volume",
        "run",
        volume.name,
      };
      if (!m.options.as && !this->default_user().empty())
      {
        args.emplace_back("--as");
        args.emplace_back(this->default_user());
      }
      auto env = elle::os::Environ{};
      m.options.to_commandline(args, env);
      for (auto const& host: _advertise_host)
      {
        args.emplace_back("--advertise-host");
        args.emplace_back(host);
      }
      args.insert(args.end(), extra_args.begin(), extra_args.end());
      if (this->_log_level)
        env.emplace("ELLE_LOG_LEVEL", _log_level.get());
      if (this->_log_path)
        env.emplace("ELLE_LOG_FILE",
                    _log_path.get()
                    + "/infinit-volume-"
                    + boost::replace_all_copy(name, "/", "_")
                    + '-'
                    + boost::posix_time::to_iso_extended_string(
                      boost::posix_time::microsec_clock::universal_time())
                    + ".log");
      ELLE_TRACE("Spawning with %s %s", args, env);
      // FIXME upgrade Process to accept env
      for (auto const& e: env)
        elle::os::setenv(e.first, e.second, true);
      m.process = std::make_unique<elle::system::Process>(args, true);
      int pid = m.process->pid();
      std::thread t([pid] {
          int status = 0;
          ::waitpid(pid, &status, 0);
      });
      t.detach();
      auto mountpoint = m.options.mountpoint;
      this->_mounts.emplace(name, std::move(m));
      if (wait_for_mount && mountpoint)
      {
        for (int i = 0; i < 100; ++i)
        {
          if (kill(pid, 0))
          {
            ELLE_WARN("infinit volume for \"%s\" not running", name);
            break;
          }
          if (is_mounted(mountpoint.get()))
            return;
          elle::reactor::sleep(100_ms);
        }
        this->stop(name);
        elle::err("unable to mount %s", name);
      }
    }

    void
    MountManager::stop(std::string const& name)
    {
      auto it = _mounts.find(name);
      if (it == _mounts.end())
        elle::err("not mounted: %s");
      auto pid = it->second.process->pid();
      ::kill(pid, SIGTERM);
      bool force_kill = true;
      for (int i = 0; i < 15; ++i)
      {
        elle::reactor::sleep(1_sec);
        if (::kill(pid, 0))
        {
          force_kill = false;
          break;
        }
      }
      if (force_kill)
        ::kill(pid, SIGKILL);
      this->_mounts.erase(it);
    }

    void
    MountManager::status(std::string const& name,
                         elle::serialization::SerializerOut& reply)
    {
      auto it = this->_mounts.find(name);
      if (it == this->_mounts.end())
        elle::err("not mounted: %s", name);
      bool live = ! kill(it->second.process->pid(), 0);
      reply.serialize("live", live);
      reply.serialize("name", name);
      if (it->second.options.mountpoint)
        reply.serialize("mountpoint", mountpoint(name));
    }

    std::vector<MountInfo>
    MountManager::status()
    {
      return elle::make_vector(_mounts, [](auto&& m) {
          return MountInfo{
            m.first,
            !kill(m.second.process->pid(), 0),
            m.second.options.mountpoint,
          };
        });
    }

    void
    MountManager::update_network(infinit::Network& network,
                                 elle::json::Object const& options)
    {
      bool updated = false;
      if (auto silodesc = optional(options, "silo"))
      {
        updated = true;
        std::unique_ptr<infinit::storage::StorageConfig> silo_config;
        if (silodesc->empty())
        {
          auto siloname = boost::replace_all_copy(network.name + "_silo",
                                                     "/", "_");
          ELLE_LOG("Creating local silo %s", siloname);
          auto path = infinit::xdg_data_home() / "blocks" / siloname;
          silo_config = std::make_unique<infinit::storage::FilesystemStorageConfig>(
            siloname, path.string(), boost::none, boost::none);
        }
        else
        {
          try
          {
            silo_config = _ifnt.silo_get(*silodesc);
          }
          catch (infinit::MissingLocalResource const&)
          {
            elle::err("Silo specification for new silo not implemented");
          }
        }
        network.model->storage = std::move(silo_config);
      }
      else if (optional(options, "no-silo"))
      {
        // XXX[Storage]: Network::model::storage
        network.model->storage.reset();
        updated = true;
      }
      if (auto portstring = optional(options, "port"))
      {
        auto dht = dynamic_cast<infinit::model::doughnut::Configuration*>(network.model.get());
        dht->port = std::stoi(*portstring);
        if (*dht->port == 0)
          dht->port.reset();
        updated = true;
      }
      if (auto rfstring = optional(options, "replication-factor"))
      {
        auto dht = dynamic_cast<infinit::model::doughnut::Configuration*>(network.model.get());
        int rf = std::stoi(*rfstring);
        auto consensus = dynamic_cast
          <infinit::model::doughnut::consensus::Paxos::Configuration*>
            (dht->consensus.get());
        consensus->replication_factor(rf);
        updated = true;
      }
      if (updated)
        _ifnt.network_save(std::move(network), true);
    }

    infinit::Network
    MountManager::create_network(elle::json::Object const& options,
                                 infinit::User const& owner)
    {
      auto netname = optional(options, "network");
      if (!netname)
        netname = this->_default_network;
      ELLE_LOG("Creating network %s", netname);
      int rf = 1;
      if (auto rfstring = optional(options, "replication-factor"))
        rf = std::stoi(*rfstring);
      // create the network
      auto kelips = [] {
        auto res = std::make_unique<infinit::overlay::kelips::Configuration>();
        res->k = 1;
        res->rpc_protocol = infinit::model::doughnut::Protocol::all;
        return res;
      }();
      auto consensus_config = [&] {
        namespace consensus = infinit::model::doughnut::consensus;
        auto res = std::make_unique<consensus::Paxos::Configuration>(
            rf,
            std::chrono::seconds(10 * 60));
        return std::unique_ptr<consensus::Configuration>{std::move(res)};
      }();
      auto port = boost::optional<int>{};
      if (auto portstring = optional(options, "port"))
        port = std::stoi(*portstring);
      auto dht =
        std::make_unique<infinit::model::doughnut::Configuration>(
          infinit::model::Address::random(0), // FIXME
          std::move(consensus_config),
          std::move(kelips),
          std::unique_ptr<infinit::storage::StorageConfig>(),
          owner.keypair(),
          std::make_shared<elle::cryptography::rsa::PublicKey>(owner.public_key),
          infinit::model::doughnut::Passport(
            owner.public_key,
            _ifnt.qualified_name(*netname, owner),
            elle::cryptography::rsa::KeyPair(owner.public_key,
                                                owner.private_key.get())),
          owner.name,
          port,
          infinit::version(),
          infinit::model::doughnut::AdminKeys(),
          std::vector<infinit::model::Endpoints>());
      auto fullname = _ifnt.qualified_name(*netname, owner);
      auto network = infinit::Network(fullname, std::move(dht), boost::none);
      _ifnt.network_save(std::move(network));
      link_network(_ifnt, fullname, options);
      auto desc = infinit::NetworkDescriptor(_ifnt.network_get(*netname, owner, true));
      if (this->_push)
        try
        {
          _ifnt.beyond_push("network", desc.name, desc, owner);
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("Failed to push network %s to beyond: %s", desc.name, e);
        }
      return _ifnt.network_get(*netname, owner, true);
    }

    void
    MountManager::create_volume(std::string const& name,
                                elle::json::Object const& options)
    {
      this->acquire_volumes();
      auto username = optional(options, "user");
      if (!username)
        username = this->_default_user;
      auto user = _ifnt.user_get(*username);
      auto netname = optional(options, "network");
      if (!netname)
      {
        if (!this->_default_network)
          elle::err("either network or a default network must be set");
        netname = this->_default_network;
      }
      auto nname = *netname;
      if (nname.find("/") == nname.npos)
        nname = *username + "/" + nname;
      infinit::Network network ([&]() -> infinit::Network {
          try
          {
            auto net = _ifnt.network_get(*netname, user, true);
            update_network(net, options);
            return net;
          }
          catch (infinit::MissingLocalResource const&)
          {
            return create_network(options, user);
          }
          catch (elle::Error const&)
          {
            link_network(_ifnt, nname, options);
            return _ifnt.network_get(*netname, user, true);
          }
      }());
      auto process = [&]
        {
          static const auto root = elle::system::self_path().parent_path();
          auto args = std::vector<std::string>{
            (root / "infinit").string(),
            "volume",
            "create",
            name,
            "--network",
            network.name,
            "--create-root",
            "--register-service",
          };
          auto mo = infinit::MountOptions{};
          if (this->_fetch)
            mo.fetch = true;
          if (this->_push)
            mo.push = true;
          mo.as = username;
          auto env = elle::os::Environ{};
          mo.to_commandline(args, env);
          return std::make_unique<elle::system::Process>(args, true);
        }();
      if (process->wait())
        elle::err("volume creation failed");
      auto qname = elle::sprintf("%s/%s", this->_default_user, name);
      auto volume = _ifnt.volume_get(qname);
      if (this->_push)
        try
        {
          _ifnt.beyond_push("volume", volume.name, volume, user);
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("failed to push %s to beyond: %s", volume.name, e);
        }
    }

    void
    MountManager::delete_volume(std::string const& name)
    {
      auto owner = _ifnt.user_get(this->default_user());
      auto qname = _ifnt.qualified_name(name, owner);
      auto path = _ifnt._volume_path(qname);
      auto volume = _ifnt.volume_get(qname);
      _ifnt.beyond_delete("volume", qname, owner, true);
      _ifnt.volume_delete(volume);
    }
  }
}
