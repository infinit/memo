#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/fwd.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class CHB
        : public blocks::ImmutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef CHB Self;
        typedef blocks::ImmutableBlock Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        CHB(Doughnut* d, elle::Buffer data, Address owner = Address::null);
        CHB(Doughnut* d, elle::Buffer data, elle::Buffer salt, Address owner = Address::null);
        CHB(CHB const& other);

      /*---------.
      | Clonable |
      `---------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool) const override;

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        void
        _seal() override;
        virtual
        blocks::ValidationResult
        _validate() const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        CHB(elle::serialization::Serializer& input,
            elle::Version const& v);
        void serialize(elle::serialization::Serializer& s,
                       elle::Version const& v) override;

      protected:
        virtual
        blocks::RemoveSignature
        _sign_remove() const override;
        virtual
        blocks::ValidationResult
        _validate_remove(blocks::RemoveSignature const& sig) const override;

      /*--------.
      | Details |
      `--------*/
      private:
        static
        elle::Buffer
        _make_salt();
        static
        Address
        _hash_address(elle::Buffer const& content, Address owner,
                      elle::Buffer const& salt,
                      elle::Version const& version);
        ELLE_ATTRIBUTE(elle::Buffer, salt);
        ELLE_ATTRIBUTE_R(Address, owner); // owner ACB address or null
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);
      };
    }
  }
}
