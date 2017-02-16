#pragma once

#include <memory>

#include <boost/filesystem.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <das/model.hh>
#include <das/serializer.hh>
#include <elle/ProducerPool.hh>
#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/doughnut/Consensus.hh>
#include <infinit/model/doughnut/Dock.hh>
#include <infinit/model/doughnut/Passport.hh>
#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    class MonitoringServer;

    namespace doughnut
    {
      namespace bmi = boost::multi_index;
      struct ACLEntry;
      DAS_SYMBOL(w);
      DAS_SYMBOL(r);
      DAS_SYMBOL(group_r);
      DAS_SYMBOL(group_w);
      struct AdminKeys
      {
        AdminKeys() {}
        AdminKeys(AdminKeys&& b) = default;
        AdminKeys(AdminKeys const& b) = default;
        AdminKeys&
        operator = (AdminKeys&& b) = default;
        std::vector<infinit::cryptography::rsa::PublicKey> r;
        std::vector<infinit::cryptography::rsa::PublicKey> w;
        std::vector<infinit::cryptography::rsa::PublicKey> group_r;
        std::vector<infinit::cryptography::rsa::PublicKey> group_w;
        using Model =
          das::Model<AdminKeys,
                     decltype(elle::meta::list(doughnut::r,
                                               doughnut::w,
                                               doughnut::group_r,
                                               doughnut::group_w))>;
      };

      class Doughnut // Doughnut. DougHnuT. Get it ?
        : public Model
        , public std::enable_shared_from_this<Doughnut>
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        typedef std::function<
          std::unique_ptr<infinit::overlay::Overlay>(
            Doughnut& dht, std::shared_ptr<Local> server)>
          OverlayBuilder;
        typedef std::function<
          std::unique_ptr<consensus::Consensus>(Doughnut&)> ConsensusBuilder;
        template <typename ... Args>
        Doughnut(Args&& ... args);
        ~Doughnut();
      private:
        struct Init;
        Doughnut(Init init);

      /*-----.
      | Time |
      `-----*/
      protected:
        virtual
        std::chrono::high_resolution_clock::time_point
        now();

      public:
        cryptography::rsa::KeyPair const&
        keys() const;
        std::shared_ptr<cryptography::rsa::KeyPair>
        keys_shared() const;
        bool
        verify(Passport const& passport,
               bool require_write,
               bool require_storage,
               bool require_sign);
        std::shared_ptr<cryptography::rsa::PublicKey>
        resolve_key(uint64_t hash);
        int
        ensure_key(std::shared_ptr<cryptography::rsa::PublicKey> const& k);
        ELLE_ATTRIBUTE_R(Address, id);
        ELLE_ATTRIBUTE(std::shared_ptr<cryptography::rsa::KeyPair>, keys);
        ELLE_ATTRIBUTE_R(std::shared_ptr<cryptography::rsa::PublicKey>, owner);
        ELLE_ATTRIBUTE_R(Passport, passport);
        ELLE_ATTRIBUTE_RX(AdminKeys, admin_keys);
        ELLE_ATTRIBUTE_R(std::unique_ptr<consensus::Consensus>, consensus)
        ELLE_ATTRIBUTE_R(std::shared_ptr<Local>, local)
        ELLE_ATTRIBUTE_RX(Dock, dock);
        ELLE_ATTRIBUTE_R(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, user_init)
        ELLE_ATTRIBUTE(
          elle::ProducerPool<std::unique_ptr<blocks::MutableBlock>>, pool)
        ELLE_ATTRIBUTE_RX(reactor::Barrier, terminating);
        ELLE_ATTRIBUTE_r(Protocol, protocol);

      public:
        struct KeyHash
        {
          KeyHash(int h, cryptography::rsa::PublicKey k)
            : hash(h)
            , key(std::make_shared(std::move(k)))
          {}

          KeyHash(int h, std::shared_ptr<cryptography::rsa::PublicKey> k)
            : hash(h)
            , key(std::move(k))
          {}

          int hash;
          std::shared_ptr<cryptography::rsa::PublicKey> key;
          cryptography::rsa::PublicKey const& raw_key() const
          {
            return *key;
          }
        };
        typedef bmi::multi_index_container<
          KeyHash,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::const_mem_fun<
                KeyHash,
                cryptography::rsa::PublicKey const&, &KeyHash::raw_key>,
                std::hash<infinit::cryptography::rsa::PublicKey>>,
            bmi::hashed_unique<
              bmi::member<KeyHash, int, &KeyHash::hash>>>> KeyCache;
        ELLE_ATTRIBUTE_R(KeyCache, key_cache);
      protected:
        virtual
        std::unique_ptr<blocks::MutableBlock>
        _make_mutable_block() const override;
        virtual
        std::unique_ptr<blocks::ImmutableBlock>
        _make_immutable_block(elle::Buffer content,
                              Address owner) const override;
        virtual
        std::unique_ptr<blocks::ACLBlock>
        _make_acl_block() const override;
        virtual
        std::unique_ptr<blocks::GroupBlock>
        _make_group_block() const override;
        virtual
        std::unique_ptr<model::User>
        _make_user(elle::Buffer const& data) const override;
        virtual
        void
        _store(std::unique_ptr<blocks::Block> block,
               StoreMode mode,
               std::unique_ptr<ConflictResolver> resolver) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;
        virtual
        void
        _fetch(std::vector<AddressVersion> const& addresses,
               std::function<void(Address, std::unique_ptr<blocks::Block>,
                 std::exception_ptr)> res) const override;
        virtual
        void
        _remove(Address address, blocks::RemoveSignature rs) override;
        friend class Local;
        ELLE_ATTRIBUTE(std::unique_ptr<MonitoringServer>, monitoring_server);

      /*------------------.
      | Service discovery |
      `------------------*/
      public:
        using Services = std::unordered_map<std::string, Address>;
        using ServicesTypes = std::unordered_map<std::string, Services>;
        ServicesTypes
        services();
        void
        service_add(std::string const& type,
                    std::string const& name,
                    elle::Buffer value);
        template <typename T>
        void
        service_add(std::string const& type,
                    std::string const&
                    name, T const& value);
      private:
        std::unique_ptr<blocks::MutableBlock>
        _services_block(bool write);
      };

      struct Configuration:
        public ModelConfig
      {
      public:
        Address id;
        std::unique_ptr<consensus::Configuration> consensus;
        std::unique_ptr<overlay::Configuration> overlay;
        cryptography::rsa::KeyPair keys;
        std::shared_ptr<cryptography::rsa::PublicKey> owner;
        Passport passport;
        boost::optional<std::string> name;
        boost::optional<int> port;
        AdminKeys admin_keys;
        std::vector<Endpoints> peers;
        using Model = das::Model<
          Configuration,
          elle::meta::List<symbols::Symbol_overlay,
                           symbols::Symbol_keys,
                           symbols::Symbol_owner,
                           symbols::Symbol_passport,
                           symbols::Symbol_name>>;


        Configuration(
          Address id,
          std::unique_ptr<consensus::Configuration> consensus,
          std::unique_ptr<overlay::Configuration> overlay,
          std::unique_ptr<storage::StorageConfig> storage,
          cryptography::rsa::KeyPair keys,
          std::shared_ptr<cryptography::rsa::PublicKey> owner,
          Passport passport,
          boost::optional<std::string> name,
          boost::optional<int> port,
          elle::Version version,
          AdminKeys admin_keys,
          std::vector<Endpoints> peers);
        Configuration(Configuration&&) = default;
        Configuration(elle::serialization::SerializerIn& input);
        ~Configuration();
        void
        serialize(elle::serialization::Serializer& s) override;
        virtual
        std::unique_ptr<infinit::model::Model>
        make(bool client,
             boost::filesystem::path const& p) override;
        std::unique_ptr<Doughnut>
        make(
          bool client,
          boost::filesystem::path const& p,
          bool async = false,
          bool cache = false,
          boost::optional<int> cach_size = {},
          boost::optional<std::chrono::seconds> cache_ttl = {},
          boost::optional<std::chrono::seconds> cache_invalidation = {},
          boost::optional<uint64_t> disk_cache_size = {},
          boost::optional<elle::Version> version = {},
          boost::optional<int> port = {},
          boost::optional<boost::asio::ip::address> listen_address = {},
          boost::optional<std::string> rdv_host = {},
          boost::optional<boost::filesystem::path> monitoring_socket_path = {});
      };

      std::string
      short_key_hash(cryptography::rsa::PublicKey const& pub);
    }
  }
}

#include <infinit/model/doughnut/Doughnut.hxx>

