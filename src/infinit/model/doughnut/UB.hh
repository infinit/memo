#ifndef INFINIT_MODEL_DOUGHNUT_UB_HH
# define INFINIT_MODEL_DOUGHNUT_UB_HH

# include <elle/attribute.hh>

# include <cryptography/rsa/PublicKey.hh>

# include <infinit/model/blocks/ImmutableBlock.hh>

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
        UB(std::string name, cryptography::rsa::PublicKey key);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, key);
        static
        Address
        hash_address(std::string const& name);

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        void
        _seal() override;
        virtual
        bool
        _validate() const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        UB(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}

#endif
