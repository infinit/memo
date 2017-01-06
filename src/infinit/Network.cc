#include <infinit/Network.hh>

#include <boost/filesystem.hpp>

#include <das/serializer.hh>

#include <reactor/http/Request.hh>

#include <infinit/Infinit.hh>
#include <infinit/MountOptions.hh>
#include <infinit/model/Endpoints.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit.Network");

namespace infinit
{
  DAS_SYMBOL(capacity);
  DAS_SYMBOL(usage);

  struct Storages
  {
    int64_t usage;
    boost::optional<int64_t> capacity;

    using Model = das::Model<
      Storages,
      decltype(elle::meta::list(infinit::usage,
                                infinit::capacity))>;
  };
}
DAS_SERIALIZE(infinit::Storages);

// FIXME: use model endpoints
struct Endpoints
{
  std::vector<std::string> addresses;
  int port;
  using Model = das::Model<
    Endpoints,
    decltype(elle::meta::list(infinit::symbols::addresses,
                              infinit::symbols::port))>;
};
DAS_SERIALIZE(Endpoints);

namespace infinit
{
  Network::Network(std::string name,
                   std::unique_ptr<model::ModelConfig> model,
                   boost::optional<std::string> description)
    : descriptor::TemplatedBaseDescriptor<Network>(std::move(name),
                                                   std::move(description))
    , model(std::move(model))
  {}

  Network::Network(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<Network>(s)
  {
    this->serialize(s);
  }

  void
  Network::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<Network>::serialize(s);
    s.serialize("model", this->model);
  }

  model::doughnut::Configuration*
  Network::dht() const
  {
    return static_cast<model::doughnut::Configuration*>(this->model.get());
  }

  bool
  Network::user_linked(infinit::User const& user) const
  {
    if (this->model == nullptr)
      return false;
    // Compare passport's public key and user public key.
    return this->dht()->passport.user() == user.public_key;
  }

  void
  Network::ensure_allowed(infinit::User const& user,
                          std::string const& action,
                          std::string const& resource) const
  {
    if (!this->user_linked(user))
      throw elle::Error(
        elle::sprintf("You cannot %s this %s as %s",
                      action, resource, user.name));
  }

  std::pair<
    std::unique_ptr<model::doughnut::Doughnut>, reactor::Thread::unique_ptr>
  Network::run(User const& user,
               MountOptions const& mo,
               bool client,
               bool enable_monitoring,
               boost::optional<elle::Version> version,
               boost::optional<int> port)
  {
    auto dht = this->run(
      user,
      client,
      mo.cache && mo.cache.get(),
      mo.cache_ram_size,
      mo.cache_ram_ttl,
      mo.cache_ram_invalidation,
      mo.async && mo.async.get(),
      mo.cache_disk_size,
      version,
      port,
      mo.listen_address
#ifndef INFINIT_WINDOWS
      , ((mo.enable_monitoring && mo.enable_monitoring.get())
         || enable_monitoring)
#endif
      );
    auto eps = model::NodeLocations{};
    if (mo.peers)
    {
      for (auto const& obj: *mo.peers)
        if (boost::filesystem::exists(obj))
          for (auto const& peer: model::endpoints_from_file(obj))
            eps.emplace_back(model::Address::null, model::Endpoints({peer}));
        else
          eps.emplace_back(model::Address::null, model::Endpoints({obj}));

    }
    reactor::Thread::unique_ptr poll_thread;
    if (mo.fetch && *mo.fetch)
    {
      beyond_fetch_endpoints(eps);
      if (mo.poll_beyond && *mo.poll_beyond > 0)
        poll_thread =
          this->make_poll_beyond_thread(*dht, eps, *mo.poll_beyond);
    }
    dht->overlay()->discover(eps);
    return {std::move(dht), std::move(poll_thread)};
  }

  reactor::Thread::unique_ptr
  Network::make_stat_update_thread(infinit::User const& self,
                                   infinit::model::doughnut::Doughnut& model)
  {
    auto notify = [&]
      {
        this->notify_storage(self, model.id());
      };
    model.local()->storage()->register_notifier(notify);
    return reactor::every(60_min, "periodic storage stat updater", notify);
  }

  reactor::Thread::unique_ptr
  Network::make_poll_beyond_thread(infinit::model::doughnut::Doughnut& model,
                                   infinit::overlay::NodeLocations const& locs_,
                                   int interval)
  {
    auto poll = [&, locs_, interval]
      {
        infinit::overlay::NodeLocations locs = locs_;
        while (true)
        {
          reactor::sleep(boost::posix_time::seconds(interval));
          infinit::overlay::NodeLocations news;
          try
          {
            this->beyond_fetch_endpoints(news);
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("exception fetching endpoints: %s", e);
            continue;
          }
          std::unordered_set<infinit::model::Address> new_addresses;
          for (auto const& n: news)
          {
            new_addresses.insert(n.id());
            auto uid = n.id();
            auto it = std::find_if(locs.begin(), locs.end(),
              [&](infinit::model::NodeLocation const& nl) { return nl.id() == uid;});
            if (it == locs.end())
            {
              ELLE_TRACE("calling discover() on new peer %s", n);
              locs.emplace_back(n);
              model.overlay()->discover({n});
            }
            else if (it->endpoints() != n.endpoints())
            {
              ELLE_TRACE("calling discover() on updated peer %s", n);
              it->endpoints() = n.endpoints();
              model.overlay()->discover({n});
            }
          }
          auto it = std::remove_if(locs.begin(), locs.end(),
            [&](infinit::model::NodeLocation const& nl)
            {
              return new_addresses.find(nl.id()) == new_addresses.end();
            });
          locs.erase(it, locs.end());
        }
      };
    return reactor::Thread::unique_ptr(new reactor::Thread("beyond poller", poll));
  }

  std::unique_ptr<model::doughnut::Doughnut>
  Network::run(User const& user,
               bool client,
               bool cache,
               boost::optional<int> cache_size,
               boost::optional<int> cache_ttl,
               boost::optional<int> cache_invalidation,
               bool async_writes,
               boost::optional<uint64_t> disk_cache_size,
               boost::optional<elle::Version> version,
               boost::optional<int> port,
               boost::optional<boost::asio::ip::address> listen,
               bool enable_monitoring)
  {
    ELLE_LOG("client version: %s", infinit::version_describe());
    auto rdv_env = elle::os::getenv("INFINIT_RDV", "rdv.infinit.sh:7890");
    boost::optional<std::string> rdv_host;
    if (!rdv_env.empty())
      rdv_host = std::move(rdv_env);
    return this->dht()->make(
      client,
      this->cache_dir(user),
      async_writes,
      cache,
      cache_size,
      cache_ttl
      ? std::chrono::seconds(cache_ttl.get())
      : boost::optional<std::chrono::seconds>(),
      cache_invalidation
      ? std::chrono::seconds(cache_invalidation.get())
      : boost::optional<std::chrono::seconds>(),
      disk_cache_size,
      std::move(version),
      std::move(port),
      std::move(listen),
      std::move(rdv_host)
#ifndef INFINIT_WINDOWS
      , enable_monitoring ? this->monitoring_socket_path(user)
      : boost::optional<boost::filesystem::path>()
#endif
      );
  }

  void
  Network::notify_storage(infinit::User const& user,
                          infinit::model::Address const& node_id)
  {
    ELLE_TRACE_SCOPE("push storage stats to %s", beyond());
    try
    {
      auto url = elle::sprintf(
        "networks/%s/stat/%s/%s", name, user.name, node_id);
      auto storage = this->dht()->storage->make();
      Storages s{storage->usage(), storage->capacity()};
      Infinit::beyond_push(
        url, "storage usage", name, std::move(s), user, false);
    }
    catch (elle::Error const& e)
    {
      ELLE_WARN("Error notifying storage size change: %s", e);
    }
  }

  boost::filesystem::path
  Network::cache_dir(User const& user) const
  {
    // "/cache" and "/async" are added by Doughnut.
    auto old_dir = xdg_state_home() / "cache" / this->name;
    auto new_dir = xdg_state_home() / "cache" / user.name / this->name;
    create_directories(new_dir / "async");
    if (boost::filesystem::exists(old_dir / "async") &&
        !boost::filesystem::is_empty(old_dir / "async"))
    {
      for (boost::filesystem::recursive_directory_iterator it(old_dir / "async");
           it != boost::filesystem::recursive_directory_iterator();
           ++it)
      {
        if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
        {
          boost::filesystem::copy_file(
            it->path(),
            new_dir / "async" / it->path().filename());
        }
      }
      boost::filesystem::remove_all(old_dir / "async");
    }
    if (boost::filesystem::exists(old_dir / "cache") &&
        !boost::filesystem::is_empty(old_dir / "cache"))
    {
      ELLE_WARN("old cache location (\"%s/cache\") is being emptied in favor "
                "of the new cache location (\"%s/cache\")",
                old_dir.string(), new_dir.string());
      boost::filesystem::remove_all(old_dir / "cache");
    }
    return new_dir;
  }

  boost::filesystem::path
  Network::monitoring_socket_path(User const& user) const
  {
#ifdef INFINIT_WINDOWS
    elle::unreachable();
#else
    return infinit::xdg_runtime_dir(std::string("/var/tmp")) /
      "infinit" / "filesystem" / "monitoring" /
      user.name / elle::sprintf("%s.sock", this->name);
#endif
  }

  void
  Network::beyond_fetch_endpoints(infinit::model::NodeLocations& hosts,
                                  Reporter report)
  {
    auto url = elle::sprintf("%s/networks/%s/endpoints", beyond(), this->name);
    reactor::http::Request r(url);
    reactor::wait(r);
    if (r.status() != reactor::http::StatusCode::OK)
    {
      throw elle::Error(
        elle::sprintf("unexpected HTTP error %s fetching endpoints for \"%s\"",
                      r.status(), this->name));
    }
    auto json = boost::any_cast<elle::json::Object>(elle::json::read(r));
    for (auto const& user: json)
    {
      try
      {
        for (auto const& node: boost::any_cast<elle::json::Object>(user.second))
        {
          infinit::model::Address uuid =
            infinit::model::Address::from_string(node.first.substr(2));
          elle::serialization::json::SerializerIn s(node.second, false);
          auto endpoints = s.deserialize<Endpoints>();
          infinit::model::Endpoints eps;
          for (auto const& addr: endpoints.addresses)
            eps.emplace_back(boost::asio::ip::address::from_string(addr),
                             endpoints.port);
          hosts.emplace_back(uuid, std::move(eps));
        }
      }
      catch (std::exception const& e)
      {
        ELLE_WARN("Exception parsing peer endpoints: %s", e.what());
      }
    }
    if (report)
      report("fetched", "endpoints for", this->name);
  }

  void
  Network::print(std::ostream& out) const
  {
    out << "Network(" << this->name << ")";
  }

  NetworkDescriptor::NetworkDescriptor(
    std::string name,
    std::unique_ptr<model::doughnut::consensus::Configuration> consensus,
    std::unique_ptr<overlay::Configuration> overlay,
    cryptography::rsa::PublicKey owner,
    elle::Version version,
    model::doughnut::AdminKeys admin_keys,
    std::vector<model::Endpoints> peers,
    boost::optional<std::string> description)
    : descriptor::TemplatedBaseDescriptor<NetworkDescriptor>(
      std::move(name), std::move(description))
    , consensus(std::move(consensus))
                                       , overlay(std::move(overlay))
                                       , owner(std::move(owner))
                                       , version(std::move(version))
                                       , admin_keys(std::move(admin_keys))
                                       , peers(std::move(peers))
  {}

  NetworkDescriptor::NetworkDescriptor(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<NetworkDescriptor>(s)
    , consensus(s.deserialize<std::unique_ptr<
                model::doughnut::consensus::Configuration>>("consensus"))
    , overlay(s.deserialize<std::unique_ptr<overlay::Configuration>>
              ("overlay"))
    , owner(s.deserialize<cryptography::rsa::PublicKey>("owner"))
    , version()
  {
    try
    {
      version = s.deserialize<elle::Version>("version");
    }
    catch (elle::serialization::Error const&)
    {
      version = elle::Version(0, 3, 0);
    }
    try
    {
      s.serialize("admin_keys", this->admin_keys);
    }
    catch (elle::serialization::Error const&)
    {
    }
    try
    {
      s.serialize("peers", this->peers);
    }
    catch (elle::serialization::Error const&)
    {}
  }

  NetworkDescriptor::NetworkDescriptor(Network&& network)
    : descriptor::TemplatedBaseDescriptor<NetworkDescriptor>(
      std::move(network.name), std::move(network.description))
    , consensus(std::move(network.dht()->consensus))
    , overlay(std::move(network.dht()->overlay))
    , owner(std::move(*network.dht()->owner))
    , version(std::move(network.dht()->version))
    , admin_keys(std::move(network.dht()->admin_keys))
    , peers(std::move(network.dht()->peers))
  {}

  NetworkDescriptor::NetworkDescriptor(NetworkDescriptor const& desc)
    : descriptor::TemplatedBaseDescriptor<NetworkDescriptor>(desc)
    , consensus(desc.consensus->clone())
    , overlay(desc.overlay->clone())
    , owner(desc.owner)
    , version(desc.version)
    , admin_keys(desc.admin_keys)
    , peers(desc.peers)
  {}

  void
  NetworkDescriptor::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<NetworkDescriptor>::serialize(s);
    s.serialize("consensus", this->consensus);
    s.serialize("overlay", this->overlay);
    s.serialize("owner", this->owner);
    try
    {
      s.serialize("version", this->version);
    }
    catch (elle::serialization::Error const&)
    {
      // Oldest versions did not specify compatibility version.
      this->version = elle::Version(0, 3, 0);
    }
    try
    {
      s.serialize("admin_keys", this->admin_keys);
    }
    catch (elle::serialization::Error const&)
    {}
    try
    {
      s.serialize("peers", this->peers);
    }
    catch (elle::serialization::Error const&)
    {}
  }
}
