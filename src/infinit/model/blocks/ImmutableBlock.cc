#include <infinit/model/blocks/ImmutableBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      ImmutableBlock::ImmutableBlock(Address address)
        : Super(address)
      {}

      ImmutableBlock::ImmutableBlock(Address address, elle::Buffer data)
        : Super(address, data)
      {}

      /*--------------.
      | Serialization |
      `--------------*/

      ImmutableBlock::ImmutableBlock(elle::serialization::Serializer& input)
        : Super(input)
      {}

      static const elle::serialization::Hierarchy<Block>::
      Register<ImmutableBlock> _register_serialization("immutable");
    }
  }
}
