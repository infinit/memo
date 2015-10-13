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

      Block::~Block()
      {}

      void
      Block::stored()
      {
        _stored();
      }
      void
      Block::_stored()
      {}

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
      Block::validate(Block const& previous) const
      {
        ELLE_DEBUG_SCOPE("%s: validate against %s", *this, previous);
        return this->_validate(previous);
      }

      ValidationResult
      Block::_validate() const
      {
        return ValidationResult::success();
      }

      ValidationResult
      Block::_validate(Block const&) const
      {
        return this->_validate();
      }

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
