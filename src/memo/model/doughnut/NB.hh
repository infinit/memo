#pragma once

#include <elle/attribute.hh>

#include <elle/cryptography/rsa/PublicKey.hh>
#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/doughnut/fwd.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class NB
        : public blocks::ImmutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = NB;
        using Super = blocks::ImmutableBlock;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        NB(Doughnut& doughnut,
           std::shared_ptr<elle::cryptography::rsa::PublicKey> owner,
           std::string name,
           elle::Buffer data,
           elle::Buffer signature = {});
        NB(Doughnut& doughnut,
           std::string name,
           elle::Buffer data,
           elle::Buffer signature = {});
        NB(NB const& other);
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);
        ELLE_ATTRIBUTE_R(std::shared_ptr<elle::cryptography::rsa::PublicKey>,
                         owner);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE_R(elle::Buffer, signature);
        static
        Address
        address(elle::cryptography::rsa::PublicKey const& owner,
                std::string const& name,
                elle::Version const& version);
        using Super::address;

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
      private:
        elle::Buffer
        _data_sign() const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        NB(elle::serialization::SerializerIn& input,
           elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}
