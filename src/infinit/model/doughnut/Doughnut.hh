#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>

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
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Doughnut(std::string name,
                 infinit::cryptography::rsa::KeyPair keys,
                 infinit::cryptography::rsa::PublicKey owner,
                 Passport passport,
                 std::unique_ptr<overlay::Overlay> overlay,
                 std::unique_ptr<Consensus> consensus = nullptr,
                 bool plain = false);
        Doughnut(infinit::cryptography::rsa::KeyPair keys,
                 infinit::cryptography::rsa::PublicKey owner,
                 Passport passport,
                 std::unique_ptr<overlay::Overlay> overlay,
                 std::unique_ptr<Consensus> consensus = nullptr,
                 bool plain = false);
        ELLE_ATTRIBUTE_R(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE(std::unique_ptr<Consensus>, consensus)
        ELLE_ATTRIBUTE_R(cryptography::rsa::KeyPair, keys);
        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, owner);
        ELLE_ATTRIBUTE_R(Passport, passport);


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
        _make_user(elle::Buffer const& data) const;
        virtual
        void
        _store(blocks::Block& block, StoreMode mode) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
        friend class Local;

      private:
        ELLE_ATTRIBUTE(bool, plain);
      };

      struct DoughnutModelConfig:
        public ModelConfig
      {
      public:
        std::unique_ptr<overlay::Configuration> overlay;
        cryptography::rsa::KeyPair keys;
        cryptography::rsa::PublicKey owner;
        Passport passport;
        boost::optional<bool> plain;
        boost::optional<std::string> name;

        DoughnutModelConfig(
          std::unique_ptr<overlay::Configuration> overlay,
          cryptography::rsa::KeyPair keys,
          cryptography::rsa::PublicKey owner,
          Passport passport,
          boost::optional<std::string> name);
        DoughnutModelConfig(elle::serialization::SerializerIn& input);
        void
        serialize(elle::serialization::Serializer& s);
        virtual
        std::unique_ptr<infinit::model::Model>
        make(std::vector<std::string> const& hosts, bool client, bool server);
      };
    }
  }
}

DAS_MODEL_FIELDS(infinit::model::doughnut::DoughnutModelConfig,
                 (overlay, keys, owner, passport, name));

#endif
