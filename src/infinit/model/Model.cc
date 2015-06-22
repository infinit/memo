#include <elle/log.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>

ELLE_LOG_COMPONENT("infinit.model.Model");

namespace infinit
{
  namespace model
  {
    Model::Model()
    {}

    template <>
    std::unique_ptr<blocks::MutableBlock>
    Model::make_block(elle::Buffer data) const
    {
      auto res = this->_make_mutable_block();
      res->data() = std::move(data);
      return res;
    }

    std::unique_ptr<blocks::MutableBlock>
    Model::_make_mutable_block() const
    {
      ELLE_TRACE_SCOPE("%s: create block", *this);
      return this->_construct_block<blocks::MutableBlock>(Address::random());
    }

    template <>
    std::unique_ptr<blocks::ImmutableBlock>
    Model::make_block(elle::Buffer data) const
    {
      return this->_make_immutable_block(std::move(data));
    }

    std::unique_ptr<blocks::ImmutableBlock>
    Model::_make_immutable_block(elle::Buffer data) const
    {
      ELLE_TRACE_SCOPE("%s: create block", *this);
      return this->_construct_block<blocks::ImmutableBlock>(Address::random(),
                                                            std::move(data));
    }

    void
    Model::store(blocks::Block& block, StoreMode mode)
    {
      block.seal();
      return this->_store(block, mode);
    }

    std::unique_ptr<blocks::Block>
    Model::fetch(Address address) const
    {
      if (auto res = this->_fetch(address))
      {
        if (!res->validate())
        {
          ELLE_WARN("%s: invalid block received for %s", *this, address);
          throw elle::Error("invalid block");
        }
        return res;
      }
      else
        throw MissingBlock(address);
    }

    void
    Model::remove(Address address)
    {
      this->_remove(address);
    }
  }
}
