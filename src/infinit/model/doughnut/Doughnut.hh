#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>
# include <boost/filesystem.hpp>

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
                 infinit::cryptography::rsa::KeyPair keys,
                 infinit::cryptography::rsa::PublicKey owner,
                 Passport passport,
                 ConsensusBuilder consensus,
                 OverlayBuilder overlay_builder,
                 boost::optional<int> port,
                 std::unique_ptr<storage::Storage> local);
        Doughnut(Address id,
                 std::string const& name,
                 infinit::cryptography::rsa::KeyPair keys,
                 infinit::cryptography::rsa::PublicKey owner,
                 Passport passport,
                 ConsensusBuilder consensus,
                 OverlayBuilder overlay_builder,
                 boost::optional<int> port,
                 std::unique_ptr<storage::Storage> local);
        ~Doughnut();
        ELLE_ATTRIBUTE_R(Address, id);
        ELLE_ATTRIBUTE_R(cryptography::rsa::KeyPair, keys);
        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, owner);
        ELLE_ATTRIBUTE_R(Passport, passport);
        ELLE_ATTRIBUTE_R(std::unique_ptr<consensus::Consensus>, consensus)
        ELLE_ATTRIBUTE_R(std::shared_ptr<Local>, local)
        ELLE_ATTRIBUTE_R(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, user_init)

      protected:
        virtual
        std::unique_ptr<blocks::MutableBlock>
        _make_mutable_block() const override;
        virtual
        std::unique_ptr<blocks::ImmutableBlock>
        _make_immutable_block(elle::Buffer content) const override;
        virtual
        std::unique_ptr<blocks::ACLBlock>
        _make_acl_block() const override;
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
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
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
        cryptography::rsa::PublicKey owner;
        Passport passport;
        boost::optional<std::string> name;
        boost::optional<int> port;

        Configuration(
          std::unique_ptr<consensus::Configuration> consensus,
          std::unique_ptr<overlay::Configuration> overlay,
          std::unique_ptr<storage::StorageConfig> storage,
          cryptography::rsa::KeyPair keys,
          cryptography::rsa::PublicKey owner,
          Passport passport,
          boost::optional<std::string> name,
          boost::optional<int> port = {});
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
             bool cache = false);
      };
    }
  }
}

DAS_MODEL_FIELDS(infinit::model::doughnut::Configuration,
                 (overlay, keys, owner, passport, name));

#endif
