#ifndef INFINIT_MODEL_DOUGHNUT_UB_HH
# define INFINIT_MODEL_DOUGHNUT_UB_HH

# include <elle/attribute.hh>

# include <cryptography/rsa/PublicKey.hh>

# include <infinit/model/blocks/ImmutableBlock.hh>

# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class UB
        : public blocks::ImmutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef UB Self;
        typedef blocks::ImmutableBlock Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        UB(Doughnut* dht, std::string name,
           Passport const& passport,
           bool reverse = false);
        UB(Doughnut* dht, std::string name,
           cryptography::rsa::PublicKey key,
           bool reverse = false);
        UB(UB const& other);
        static
        Address
        hash_address(std::string const& name, Doughnut const& dht);
        static
        Address
        hash_address(cryptography::rsa::PublicKey const& key,
                     Doughnut const& dht);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, key);
        ELLE_ATTRIBUTE_R(bool, reverse);
        ELLE_ATTRIBUTE_R(boost::optional<Passport>, passport);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone() const override;

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        void
        _seal(boost::optional<int> version) override;
        virtual
        blocks::ValidationResult
        _validate() const override;
        virtual
        blocks::RemoveSignature
        _sign_remove() const override;
        virtual
        blocks::ValidationResult
        _validate_remove(blocks::RemoveSignature const& sig) const override;
        virtual
        blocks::ValidationResult
        _validate(const Block& new_block) const override;
      /*--------------.
      | Serialization |
      `--------------*/
      public:
        UB(elle::serialization::SerializerIn& input,
           elle::Version const& version);
        virtual
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };
    }
  }
}

#endif
