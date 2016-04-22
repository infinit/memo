#include <elle/log.hh>
#include <elle/utils.hh>

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
        , _validated(false)
      {}

      Block::Block(Address address, elle::Buffer data)
        : _address(std::move(address))
        , _data(std::move(data))
        , _validated(false)
      {}

      Block::Block(Block const& other)
        : _address(other._address)
        , _data(other._data)
        , _validated(other._validated)
      {}

      /*---------.
      | Clonable |
      `---------*/

      std::unique_ptr<Block>
      Block::clone() const
      {
        return elle::make_unique<Block>(this->address(), this->data());
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
      Block::seal(boost::optional<int> version)
      {
        ELLE_DEBUG_SCOPE("%s: seal", *this);
        this->_seal(version);
      }

      void
      Block::_seal(boost::optional<int> version)
      {}

      ValidationResult
      Block::validate(Model const& model) const
      {
        ELLE_TRACE_SCOPE("%s: validate", *this);
        if (this->_validated)
          return ValidationResult::success();
        ValidationResult res = this->_validate(model);
        elle::unconst(this)->_validated = res;
        return res;
      }

      ValidationResult
      Block::_validate(Model const&) const
      {
        return ValidationResult::success();
      }

      ValidationResult
      Block::validate(Model const& model, Block const& new_block) const
      {
        ELLE_TRACE_SCOPE("%s: validate against previous block %s",
                         *this, new_block);
        return this->_validate(model, new_block);
      }

      ValidationResult
      Block::_validate(Model const&, Block const& new_block) const
      {
        if (this->address() != new_block.address())
          return ValidationResult::failure(
            elle::sprintf("Addresses do not match (old %s new %s)",
              this->address(), new_block.address()));
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

      RemoveSignature
      Block::sign_remove(Model& model) const
      {
        return this->_sign_remove(model);
      }

      RemoveSignature
      Block::_sign_remove(Model&) const
      {
        return RemoveSignature();
      }

      ValidationResult
      Block::validate_remove(Model& model,
                             RemoveSignature const & sig) const
      {
        return this->_validate_remove(model, sig);
      }

      ValidationResult
      Block::_validate_remove(Model& model,
                              RemoveSignature const& sig) const
      {
        return ValidationResult::success();
      }

      /*--------------.
      | Serialization |
      `--------------*/

      Block::Block(elle::serialization::Serializer& input,
                   elle::Version const& version)
      : _validated(false)
      {
        this->serialize(input, version);
      }

      void
      Block::serialize(elle::serialization::Serializer& s,
                       elle::Version const&)
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
        elle::fprintf(output, "%f(%f)", elle::type_info(*this), this->_address);
      }

      RemoveSignature::RemoveSignature()
      {}

      RemoveSignature::RemoveSignature(elle::serialization::Serializer& s)
      {
        this->serialize(s);
      }

      RemoveSignature::RemoveSignature(RemoveSignature && other)
      : block(std::move(other.block))
      , group_key(std::move(other.group_key))
      , group_index(std::move(other.group_index))
      , signature_key(std::move(other.signature_key))
      , signature(std::move(other.signature))
      {
      }

      RemoveSignature&
      RemoveSignature::operator = (RemoveSignature && other)
      {
        this->block = std::move(other.block);
        this->group_key = std::move(other.group_key);
        this->group_index = std::move(other.group_index);
        this->signature_key = std::move(other.signature_key);
        this->signature = std::move(other.signature);
        return *this;
      }

      RemoveSignature::RemoveSignature(RemoveSignature const& other)
      : group_key(other.group_key)
      , group_index(other.group_index)
      , signature_key(other.signature_key)
      , signature(other.signature)
      {
        if (other.block)
          block = other.block->clone();
      }
      void
      RemoveSignature::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("block", block);
        s.serialize("key", signature_key);
        s.serialize("signature", signature);
        s.serialize("group_key", group_key);
        s.serialize("group_index", group_index);
      }
    }
  }
}
