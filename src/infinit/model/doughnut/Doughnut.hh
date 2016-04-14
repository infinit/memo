#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>
# include <boost/filesystem.hpp>

# include <das/model.hh>
# include <das/serializer.hh>
# include <elle/ProducerPool.hh>
# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/Model.hh>
# include <infinit/model/doughnut/Consensus.hh>
# include <infinit/model/doughnut/Passport.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      struct ACLEntry;
      struct AdminKeys
      {
        AdminKeys() {}
        AdminKeys(AdminKeys&& b) = default;
        AdminKeys(AdminKeys const& b) = default;
        //AdminKeys& operator = (AdminKeys const& b) = default;
        std::vector<infinit::cryptography::rsa::PublicKey> r;
        std::vector<infinit::cryptography::rsa::PublicKey> w;
        std::vector<infinit::cryptography::rsa::PublicKey> group_r;
        std::vector<infinit::cryptography::rsa::PublicKey> group_w;
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
            Doughnut& dht, Address id, std::shared_ptr<Local> server)>
          OverlayBuilder;
        typedef std::function<
          std::unique_ptr<consensus::Consensus>(Doughnut&)> ConsensusBuilder;
        Doughnut(Address id,
                 std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys,
                 std::shared_ptr<infinit::cryptography::rsa::PublicKey> owner,
                 Passport passport,
                 ConsensusBuilder consensus,
                 OverlayBuilder overlay_builder,
                 boost::optional<int> port,
                 std::unique_ptr<storage::Storage> local,
                 boost::optional<elle::Version> version = {},
                 AdminKeys const& admin_keys = {});
        Doughnut(Address id,
                 std::string const& name,
                 std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys,
                 std::shared_ptr<infinit::cryptography::rsa::PublicKey> owner,
                 Passport passport,
                 ConsensusBuilder consensus,
                 OverlayBuilder overlay_builder,
                 boost::optional<int> port,
                 std::unique_ptr<storage::Storage> local,
                 boost::optional<elle::Version> version = {},
                 AdminKeys const& admin_keys = {});
        ~Doughnut();

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
        ELLE_ATTRIBUTE_R(Address, id);
        ELLE_ATTRIBUTE(std::shared_ptr<cryptography::rsa::KeyPair>, keys);
        ELLE_ATTRIBUTE_R(std::shared_ptr<cryptography::rsa::PublicKey>, owner);
        ELLE_ATTRIBUTE_R(Passport, passport);
        ELLE_ATTRIBUTE_R(std::unique_ptr<consensus::Consensus>, consensus)
        ELLE_ATTRIBUTE_R(std::shared_ptr<Local>, local)
        ELLE_ATTRIBUTE_R(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, user_init)
        ELLE_ATTRIBUTE(
          elle::ProducerPool<std::unique_ptr<blocks::MutableBlock>>, pool)
        ELLE_ATTRIBUTE_RX(AdminKeys, admin_keys);

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
        _remove(Address address, blocks::RemoveSignature rs) override;
        friend class Local;
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
          AdminKeys admin_keys);
        Configuration(Configuration&&) = default;
        Configuration(elle::serialization::SerializerIn& input);
        ~Configuration();
        void
        serialize(elle::serialization::Serializer& s);
        virtual
        std::unique_ptr<infinit::model::Model>
        make(overlay::NodeEndpoints const& hosts,
             bool client,
             boost::filesystem::path const& p);
        std::unique_ptr<Doughnut>
        make(overlay::NodeEndpoints const& hosts,
             bool client,
             boost::filesystem::path const& p,
             bool async = false,
             bool cache = false,
             boost::optional<int> cach_size = {},
             boost::optional<std::chrono::seconds> cache_ttl = {},
             boost::optional<std::chrono::seconds> cache_invalidation = {},
             boost::optional<uint64_t> disk_cache_size = {},
             boost::optional<elle::Version> version = {},
             boost::optional<int> port = {});
      };
    }
  }
}

DAS_MODEL_FIELDS(infinit::model::doughnut::Configuration,
                 (overlay, keys, owner, passport, name));

DAS_MODEL(infinit::model::doughnut::AdminKeys, (r, w, group_r, group_w), DasAdminKeys);
DAS_MODEL_DEFAULT(infinit::model::doughnut::AdminKeys, DasAdminKeys);
DAS_MODEL_SERIALIZE(infinit::model::doughnut::AdminKeys);

#endif
