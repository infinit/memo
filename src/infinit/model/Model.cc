#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    Model::Model()
    {}

    std::unique_ptr<blocks::Block>
    Model::make_block() const
    {
      return this->_make_block();
    }

    void
    Model::store(blocks::Block& block)
    {
      return this->_store(block);
    }

    std::unique_ptr<blocks::Block>
    Model::fetch(Address address) const
    {
      if (auto res = this->_fetch(address))
        return res;
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
