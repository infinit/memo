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
        using Self = infinit::model::doughnut::CHB;
        using Super = blocks::ImmutableBlock;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        CHB(Doughnut* d,
            elle::Buffer data,
            Address owner = Address::null);
        CHB(Doughnut* d,
            elle::Buffer data,
            elle::Buffer salt,
            Address owner = Address::null);
        CHB(CHB const& other);
        CHB(CHB&& other);

      /*---------.
      | Clonable |
      `---------*/
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

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        CHB(elle::serialization::Serializer& input,
            elle::Version const& v);
        void serialize(elle::serialization::Serializer& s,
                       elle::Version const& v) override;
        static
        blocks::RemoveSignature
        sign_remove(Model& model, Address chb, Address owner);
      protected:
        blocks::RemoveSignature
        _sign_remove(Model& model) const override;
        blocks::ValidationResult
        _validate_remove(Model& model,
                         blocks::RemoveSignature const& sig) const override;

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
      };
    }
  }
}
