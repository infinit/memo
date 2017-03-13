#ifndef INFINIT_MODEL_BLOCKS_IMMUTABLE_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_IMMUTABLE_BLOCK_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class ImmutableBlock
        : public Block
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = infinit::model::blocks::ImmutableBlock;
        using Super = infinit::model::blocks::Block;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ImmutableBlock(ImmutableBlock&&) = default;

      protected:
        ImmutableBlock(Address address);
        ImmutableBlock(Address address, elle::Buffer data);
        ImmutableBlock(ImmutableBlock const& other);
        friend class infinit::model::Model;

      /*-------.
      | Clone  |
      `-------*/
      public:

      std::unique_ptr<Block>
      clone() const override;


      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ImmutableBlock(elle::serialization::Serializer& input,
                       elle::Version const& version);
      };
    }
  }
}

#endif
