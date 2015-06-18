#include <infinit/model/blocks/Block.hh>

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

      Block::~Block()
      {}

      /*--------.
      | Content |
      `--------*/

      elle::Buffer const&
      Block::data() const
      {
        return this->_data;
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
        this->_seal();
      }

      void
      Block::_seal()
      {}

      bool
      Block::validate() const
      {
        return this->_validate();
      }

      bool
      Block::_validate() const
      {
        return true;
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
        elle::fprintf(output, "Block(%x, %x)",
                      this->_address, this->_data);
      }
    }
  }
}
