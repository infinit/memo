#pragma once

#include <elle/attribute.hh>
#include <elle/cryptography/rsa/PublicKey.hh>

#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Passport.hh>

namespace memo
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
        using Self = memo::model::doughnut::UB;
        using Super = blocks::ImmutableBlock;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        UB(Doughnut* dht, std::string name,
           Passport const& passport,
           bool reverse = false);
        UB(Doughnut* dht, std::string name,
           elle::cryptography::rsa::PublicKey key,
           bool reverse = false);
        UB(UB const& other);
        static
        Address
        hash_address(std::string const& name, Doughnut const& dht);
        static
        Address
        hash_address(elle::cryptography::rsa::PublicKey const& key,
                     Doughnut const& dht);
        static
        elle::Buffer
        hash(elle::cryptography::rsa::PublicKey const& key);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE_R(elle::cryptography::rsa::PublicKey, key);
        ELLE_ATTRIBUTE_R(bool, reverse);
        ELLE_ATTRIBUTE_R(boost::optional<Passport>, passport);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);

      /*-------.
      | Clone  |
      `-------*/
      public:
        std::unique_ptr<blocks::Block>
        clone() const override;

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        void
        _seal(boost::optional<int> version) override;
        blocks::ValidationResult
        _validate(Model const& model, bool writing) const override;
        blocks::RemoveSignature
        _sign_remove(Model& model) const override;
        blocks::ValidationResult
        _validate_remove(Model& model,
                         blocks::RemoveSignature const& sig) const override;
        blocks::ValidationResult
        _validate(Model const& model, const Block& new_block) const override;
      /*--------------.
      | Serialization |
      `--------------*/
      public:
        UB(elle::serialization::SerializerIn& input,
           elle::Version const& version);
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
