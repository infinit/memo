#include <memo/model/blocks/MutableBlock.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      char const* MutableBlock::type = "mutable";

      MutableBlock::MutableBlock(Address address,
                                 elle::Buffer data,
                                 Address owner)
        : Super(address, std::move(data), std::move(owner))
        , _data_changed(true)
      {}

      void
      MutableBlock::data(elle::Buffer data)
      {
        this->_data = std::move(data);
        this->_data_changed = true;
      }

      void
      MutableBlock::data(std::function<void (elle::Buffer&)> transformation)
      {
        transformation(this->_data);
        this->_data_changed = true;
      }

      /*-------.
      | Clone  |
      `-------*/
      std::unique_ptr<Block>
      MutableBlock::clone() const
      {
        return std::unique_ptr<Block>(new MutableBlock(*this));
      }

      /*--------------.
      | Serialization |
      `--------------*/

      MutableBlock::MutableBlock(elle::serialization::Serializer& input,
                                 elle::Version const& version)
        : Super(input, version)
        , _data_changed(false)
      {
        this->_serialize(input);
      }

      void
      MutableBlock::serialize(elle::serialization::Serializer& s,
                              elle::Version const& v)
      {
        this->Super::serialize(s, v);
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
