#include <infinit/model/blocks/MutableBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      MutableBlock::MutableBlock(Address address)
        : Super(address)
        , _data_changed(true)
      {}

      MutableBlock::MutableBlock(Address address, elle::Buffer data)
        : Super(address, data)
        , _data_changed(true)
      {}

      void
      MutableBlock::data(elle::Buffer data)
      {
        // FIXME: Don't change this, subclasses override the other data
        // setter version.
        this->data([&] (elle::Buffer& _data) { _data = std::move(data); });
      }

      void
      MutableBlock::data(std::function<void (elle::Buffer&)> transformation)
      {
        transformation(this->_data);
        this->_data_changed = true;
      }

      /*--------------.
      | Serialization |
      `--------------*/

      MutableBlock::MutableBlock(elle::serialization::Serializer& input)
        : Super(input)
        , _data_changed(false)
      {
        this->_serialize(input);
      }

      void
      MutableBlock::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      void
      MutableBlock::_serialize(elle::serialization::Serializer&)
      {}

      static const elle::serialization::Hierarchy<Block>::
      Register<MutableBlock> _register_serialization("mutable");
    }
  }
}
