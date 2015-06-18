#include <elle/log.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>
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
    Model::make_block() const
    {
      return this->_make_mutable_block();
    }

    void
    Model::store(blocks::Block& block)
    {
      block.seal();
      return this->_store(block);
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
