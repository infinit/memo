#pragma once

#include <elle/das/serializer.hh>

#include <memo/User.hh>
#include <memo/fwd.hh>
#include <memo/descriptor/TemplatedBaseDescriptor.hh>
#include <memo/model/Model.hh>
#include <memo/model/doughnut/Doughnut.hh>

namespace memo
{
  ELLE_DAS_SYMBOL(capacity);
  ELLE_DAS_SYMBOL(usage);

  struct Storages
  {
    int64_t usage;
    boost::optional<int64_t> capacity;

    using Model = elle::das::Model<
      Storages,
      decltype(elle::meta::list(memo::usage,
                                memo::capacity))>;
  };
}
ELLE_DAS_SERIALIZE(memo::Storages);


namespace memo
{
  class Network
    : public descriptor::TemplatedBaseDescriptor<Network>
  {
  public:
    using Reporter = std::function<void (std::string const&,
                                         std::string const&,
                                         std::string const&)>;
    Network(std::string name,
            std::unique_ptr<model::ModelConfig> model = nullptr,
            boost::optional<std::string> description = {});

    Network(elle::serialization::SerializerIn& s);

    void
    serialize(elle::serialization::Serializer& s) override;

    model::doughnut::Configuration*
    dht() const;

    bool
    user_linked(memo::User const& user) const;

    void
    ensure_allowed(memo::User const& user,
                   std::string const& action,
                   std::string const& resource = "network") const;

    using ThreadedOverlay
    = std::pair<std::unique_ptr<model::doughnut::Doughnut>,
                elle::reactor::Thread::unique_ptr>;
    ThreadedOverlay
    run(User const& user,
        MountOptions const& mo,
        bool client = false,
        bool enable_monitoring = true,
        boost::optional<elle::Version> version = {},
        boost::optional<int> port = {});

    elle::reactor::Thread::unique_ptr
    make_stat_update_thread(memo::Memo const& memo,
                            memo::User const& self,
                            memo::model::doughnut::Doughnut& model);

    /// @param interval in seconds.
    elle::reactor::Thread::unique_ptr
    make_poll_hub_thread(memo::model::doughnut::Doughnut& model,
                         memo::overlay::NodeLocations const& locs,
                         int interval);

    std::unique_ptr<model::doughnut::Doughnut>
    run(User const& user,
        bool client = false,
        bool cache = false,
        boost::optional<int> cache_size = {},
        boost::optional<int> cache_ttl = {},
        boost::optional<int> cache_invalidation = {},
        bool async_writes = false,
        boost::optional<uint64_t> disk_cache_size = {},
        boost::optional<elle::Version> version = {},
        boost::optional<int> port = {},
        boost::optional<boost::asio::ip::address> listen = {},
        boost::optional<bool> resign_on_shutdown = {},
        bool enable_monitoring = true);

    void
    notify_storage(memo::Memo const& memoy,
                   memo::User const& user,
                   memo::model::Address const& node_id);

    boost::filesystem::path
    cache_dir(User const& user) const;

    boost::filesystem::path
    monitoring_socket_path(User const& user) const;

    void
    hub_fetch_endpoints(memo::model::NodeLocations& hosts,
                        Reporter report = {});

    void
    print(std::ostream& out) const override;

    std::unique_ptr<model::ModelConfig> model;
  };

  struct NetworkDescriptor
    : public descriptor::TemplatedBaseDescriptor<NetworkDescriptor>
  {
    NetworkDescriptor(
      std::string name,
      std::unique_ptr<model::doughnut::consensus::Configuration> consensus,
      std::unique_ptr<overlay::Configuration> overlay,
      elle::cryptography::rsa::PublicKey owner,
      elle::Version version,
      model::doughnut::AdminKeys admin_keys,
      std::vector<model::Endpoints> peers,
      boost::optional<std::string> description,
      elle::DurationOpt tcp_heartbeat,
      model::doughnut::EncryptOptions encrypt_options);

    NetworkDescriptor(elle::serialization::SerializerIn& s);

    NetworkDescriptor(Network&& network);

    NetworkDescriptor(NetworkDescriptor const& desc);

    void
    serialize(elle::serialization::Serializer& s) override;

    std::unique_ptr<model::doughnut::consensus::Configuration> consensus;
    std::unique_ptr<overlay::Configuration> overlay;
    elle::cryptography::rsa::PublicKey owner;
    elle::Version version;
    model::doughnut::AdminKeys admin_keys;
    std::vector<model::Endpoints> peers;
    elle::DurationOpt tcp_heartbeat;
    model::doughnut::EncryptOptions encrypt_options;
  };
}
