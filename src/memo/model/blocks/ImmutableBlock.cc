#include <memo/model/blocks/ImmutableBlock.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      char const* ImmutableBlock::type = "immutable";

      ImmutableBlock::ImmutableBlock(Address address,
                                     elle::Buffer data,
                                     Address owner)
        : Super(address, std::move(data), std::move(owner))
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
