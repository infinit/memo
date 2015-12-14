#ifndef INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class MutableBlock
        : public Block
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef MutableBlock Self;
        typedef Block Super;

      /*-------------.
      | Construction |
      `-------------*/
        MutableBlock(MutableBlock const& other);
      protected:
        MutableBlock(Address address);
        MutableBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;
        bool _data_changed;

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<Block>
        clone(bool seal_copy=true) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        MutableBlock(elle::serialization::Serializer& input,
                     elle::Version const& version);
        virtual
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& s);

      /*--------.
      | Content |
      `--------*/
      public:
        using Super::data;
        virtual
        int
        version() const /* = 0 */ { return 0; }; // FIXME
        virtual
        void
        data(elle::Buffer data);
        virtual
        void
        data(std::function<void (elle::Buffer&)> transformation);
        ELLE_ATTRIBUTE_RW(bool, is_local, protected);
      };
    }
  }
}

#endif
