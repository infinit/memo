#include <infinit/model/blocks/ACLBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      /*-------------.
      | Construction |
      `-------------*/

      ACLBlock::ACLBlock(Address address)
        : Super(address)
      {}

      ACLBlock::ACLBlock(Address address, elle::Buffer data)
        : Super(address, data)
      {}

      /*--------------.
      | Serialization |
      `--------------*/

      ACLBlock::ACLBlock(elle::serialization::Serializer& input)
        : Super(input)
      {
        this->_serialize(input);
      }

      void
      ACLBlock::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      void
      ACLBlock::_serialize(elle::serialization::Serializer&)
      {}
    }
  }
}
