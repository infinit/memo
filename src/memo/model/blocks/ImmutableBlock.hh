#pragma once

#include <memo/model/blocks/Block.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      class ImmutableBlock
        : public Block
        , private InstanceTracker<ImmutableBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = ImmutableBlock;
        using Super = Block;
        static char const* type;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ImmutableBlock(ImmutableBlock&&) = default;

      protected:
        ImmutableBlock(Address address,
                       elle::Buffer data = {},
                       Address owner = Address::null);
        ImmutableBlock(ImmutableBlock const& other);
        friend class memo::model::Model;

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
