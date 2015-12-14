#ifndef INFINIT_MODEL_BLOCKS_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_BLOCK_HH

# include <elle/Buffer.hh>
# include <elle/Printable.hh>
# include <elle/Clonable.hh>

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
        , public elle::serialization::VirtuallySerializable<true>
        , public elle::Clonable<Block>
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Block(Address address);
        Block(Address address, elle::Buffer data);
        Block(Block const& other);
        friend class infinit::model::Model;
        virtual
        ~Block() = default;

      /*---------.
      | Clonable |
      `---------*/
      public:
        virtual
        std::unique_ptr<Block>
        clone(bool seal_copy) const;
        virtual
        std::unique_ptr<Block>
        clone() const override;
      /*--------.
      | Content |
      `--------*/
      public:
        virtual
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
        void
        stored(); // called right after a successful store
      protected:
        virtual
        void
        _seal();
        virtual
        ValidationResult
        _validate() const;
        virtual
        void
        _stored();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        static constexpr char const* virtually_serializable_key = "type";
        Block(elle::serialization::Serializer& input,
              elle::Version const& version);
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
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
