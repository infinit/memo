#include <infinit/model/faith/Faith.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>

#include <cryptography/oneway.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.faith.Faith");

namespace infinit
{
  namespace model
  {
    namespace faith
    {
      Faith::Faith(std::unique_ptr<storage::Storage> storage)
        : _storage(std::move(storage))
      {}

      void
      Faith::_store(blocks::Block& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        auto& data = block.data();
        this->_storage->set(block.address(),
                            data,
                            mode == STORE_ANY || mode == STORE_INSERT,
                            mode == STORE_ANY || mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Faith::_fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        try
        {
          return this->_construct_block<blocks::MutableBlock>
            (address, this->_storage->get(address));
        }
        catch (infinit::storage::MissingKey const&)
        {
          return nullptr;
        }
      }

      void
      Faith::_remove(Address address)
      {
        ELLE_TRACE_SCOPE("%s: remove block at %x", *this, address);
        try
        {
          this->_storage->erase(address);
        }
        catch (infinit::storage::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }
    }
  }
}
