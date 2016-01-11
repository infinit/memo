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

      ImmutableBlock::ImmutableBlock(ImmutableBlock const& other)
        : Super(other)
      {}

      /*-------.
      | Clone  |
      `-------*/
      std::unique_ptr<Block>
      ImmutableBlock::clone() const
      {
        return std::unique_ptr<Block>(new ImmutableBlock(*this));
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ImmutableBlock::ImmutableBlock(elle::serialization::Serializer& input,
                                     elle::Version const& version)
        : Super(input, version)
      {}

      static const elle::serialization::Hierarchy<Block>::
      Register<ImmutableBlock> _register_serialization("immutable");
    }
  }
}
