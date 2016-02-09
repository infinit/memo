#ifndef INFINIT_MODEL_BLOCKS_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_BLOCK_HH

# include <elle/Buffer.hh>
# include <elle/Printable.hh>
# include <elle/Clonable.hh>

# include <cryptography/rsa/PublicKey.hh>

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
      struct RemoveSignature
      {
        RemoveSignature();
        RemoveSignature(RemoveSignature const& other);
        RemoveSignature(RemoveSignature && other);
        RemoveSignature(elle::serialization::Serializer& input);
        RemoveSignature& operator = (RemoveSignature && other);
        void serialize(elle::serialization::Serializer& s);
        std::unique_ptr<Block> block;
        boost::optional<cryptography::rsa::PublicKey> group_key;
        boost::optional<int> group_index;
        boost::optional<cryptography::rsa::PublicKey> signature_key;
        boost::optional<elle::Buffer> signature;
      };

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
        ELLE_ATTRIBUTE_R(Address, address, protected);
        elle::Buffer
        take_data();

      protected:
        elle::Buffer _data;

      /*-----------.
      | Validation |
      `-----------*/
      public:
        void
        seal(boost::optional<int> version = {});
        ValidationResult
        validate() const;
        ValidationResult
        validate(const Block& new_block) const;
        void
        stored(); // called right after a successful store
        RemoveSignature
        sign_remove() const; // generate signature for removal request
        ValidationResult
        validate_remove(RemoveSignature const& sig) const;
      protected:
        virtual
        void
        _seal(boost::optional<int> version);
        virtual
        ValidationResult
        _validate() const;
        virtual
        ValidationResult
        _validate(const Block& new_block) const;
        virtual
        void
        _stored();
        virtual
        RemoveSignature
        _sign_remove() const;
        virtual
        ValidationResult
        _validate_remove(RemoveSignature const& sig) const;

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
