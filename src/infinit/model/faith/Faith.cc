#include <infinit/model/faith/Faith.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>

#include <cryptography/oneway.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
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

      std::unique_ptr<blocks::Block>
      Faith::_make_block() const
      {
        ELLE_TRACE_SCOPE("%s: create block", *this);
        // Hash a UUID to get a random address.  Like using a deathstar to blow
        // a mosquito and I like it.
        auto id = boost::uuids::basic_random_generator<boost::mt19937>()();
        auto hash = cryptography::oneway::hash(
          cryptography::Plain(elle::ConstWeakBuffer(id.data, id.static_size())),
          cryptography::oneway::Algorithm::sha256);
        ELLE_ASSERT_GTE(hash.buffer().size(), sizeof(Address::Value));
        Address address(hash.buffer().contents());
        auto res = std::unique_ptr<blocks::Block>(new blocks::Block(address));
        return res;
      }

      void
      Faith::_store(blocks::Block& block)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        auto& data = block.data();
        this->_storage->set(block.address(),
                            data,
                            true, true);
      }

      std::unique_ptr<blocks::Block>
      Faith::_fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        try
        {
          return std::unique_ptr<blocks::Block>
            (new blocks::Block(address, this->_storage->get(address)));
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
