#include <infinit/model/blocks/MutableBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      MutableBlock::MutableBlock(Address address)
        : Super(address)
      {}

      MutableBlock::MutableBlock(Address address, elle::Buffer data)
        : Super(address, data)
      {}

      elle::Buffer&
      MutableBlock::data()
      {
        return this->_data;
      }
    }
  }
}
