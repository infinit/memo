#include <infinit/model/blocks/ImmutableBlock.hh>

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
        CHB(elle::Buffer data);
        CHB(elle::Buffer data, elle::Buffer salt);
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
        CHB(elle::serialization::Serializer& input);
        void serialize(elle::serialization::Serializer& s) override;

      /*--------.
      | Details |
      `--------*/
      private:
        static
        elle::Buffer
        _make_salt();
        static
        Address
        _hash_address(elle::Buffer const& content, elle::Buffer const& salt);
        ELLE_ATTRIBUTE(elle::Buffer, salt);
      };
    }
  }
}
