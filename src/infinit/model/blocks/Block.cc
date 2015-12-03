#include <elle/log.hh>

#include <infinit/model/blocks/Block.hh>

ELLE_LOG_COMPONENT("infinit.model.blocks.Block");

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      /*-------------.
      | Construction |
      `-------------*/

      Block::Block(Address address)
        : _address(std::move(address))
        , _data()
      {}

      Block::Block(Address address, elle::Buffer data)
        : _address(std::move(address))
        , _data(std::move(data))
      {}

      Block::Block(Block const& other)
        : _address(other._address)
        , _data(other._data)
      {}

      /*---------.
      | Clonable |
      `---------*/

      std::unique_ptr<Block>
      Block::clone(bool) const
      {
        return elle::make_unique<Block>(this->address(), this->data());
      }

      std::unique_ptr<Block>
      Block::clone() const
      {
        return clone(true);
      }

      /*--------.
      | Content |
      `--------*/

      elle::Buffer const&
      Block::data() const
      {
        return this->_data;
      }

      elle::Buffer
      Block::take_data()
      {
        return std::move(this->_data);
      }

      bool
      Block::operator ==(Block const& rhs) const
      {
        return rhs._address == this->_address && rhs._data == this->_data;
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      Block::seal()
      {
        ELLE_DEBUG_SCOPE("%s: seal", *this);
        this->_seal();
      }

      void
      Block::_seal()
      {}

      ValidationResult
      Block::validate() const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        return this->_validate();
      }

      ValidationResult
      Block::_validate() const
      {
        return ValidationResult::success();
      }

      void
      Block::stored()
      {
        _stored();
      }
      void
      Block::_stored()
      {}

      /*--------------.
      | Serialization |
      `--------------*/

      Block::Block(elle::serialization::Serializer& input)
      {
        this->serialize(input);
      }

      void
      Block::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("address", this->_address);
        s.serialize("data", this->_data);
      }

      static const elle::serialization::Hierarchy<Block>::
      Register<Block> _register_serialization("block");

      /*----------.
      | Printable |
      `----------*/

      void
      Block::print(std::ostream& output) const
      {
        elle::fprintf(
          output, "%s(%f, %f)",
          elle::type_info(*this).name(), this->_address, this->_data);
      }
    }
  }
}
