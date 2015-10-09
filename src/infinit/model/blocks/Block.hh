#ifndef INFINIT_MODEL_BLOCKS_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_BLOCK_HH

# include <elle/Buffer.hh>
# include <elle/Printable.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/blocks/ValidationResult.hh>
# include <infinit/model/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class Block
        : public elle::Printable
        , public elle::serialization::VirtuallySerializable
      {
      /*-------------.
      | Construction |
      `-------------*/
      protected:
        Block(Address address);
        Block(Address address, elle::Buffer data);
        friend class infinit::model::Model;
      public:
        virtual
        ~Block() = 0;

      /*--------.
      | Content |
      `--------*/
      public:
        bool
        operator ==(Block const& rhs) const;
        virtual
        elle::Buffer const&
        data() const;
        ELLE_ATTRIBUTE_R(Address, address);
        elle::Buffer
        take_data();

      protected:
        elle::Buffer _data;

      /*-----------.
      | Validation |
      `-----------*/
      public:
        void
        seal();
        ValidationResult
        validate() const;
        ValidationResult
        validate(Block const& previous) const;
        void stored(); // called right after a successful store
      protected:
        virtual
        void
        _seal();
        virtual
        ValidationResult
        _validate() const;
        virtual
        ValidationResult
        _validate(Block const& previous) const;
        virtual
        void
        _stored();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        static constexpr char const* virtually_serializable_key = "type";
        Block(elle::serialization::Serializer& input);
        void
        serialize(elle::serialization::Serializer& s);
        typedef infinit::serialization_tag serialization_tag;

      /*----------.
      | Printable |
      `----------*/

      public:
        virtual
        void
        print(std::ostream& output) const override;
      };
    }
  }
}

#endif
