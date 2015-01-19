#include <infinit/model/paranoid/Paranoid.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <cryptography/hash.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.paranoid.Paranoid");

namespace infinit
{
  namespace model
  {
    namespace paranoid
    {
      Paranoid::Paranoid(infinit::cryptography::KeyPair keys,
                         std::unique_ptr<storage::Storage> storage)
        : _keys(std::move(keys))
        , _storage(std::move(storage))
      {}

      Paranoid::~Paranoid()
      {}

      std::unique_ptr<blocks::Block>
      Paranoid::_make_block() const
      {
        ELLE_TRACE_SCOPE("%s: create block", *this);
        // Hash a UUID to get a random address.  Like using a deathstar to blow
        // a mosquito and I like it.
        auto id = boost::uuids::basic_random_generator<boost::mt19937>()();
        auto hash = cryptography::hash::sha256(
          elle::ConstWeakBuffer(id.data, id.static_size()));
        ELLE_ASSERT_GTE(hash.size(), sizeof(Address::Value));
        Address address(hash.contents());
        return std::unique_ptr<blocks::Block>(new blocks::Block(address));
      }

      struct CryptedBlock
      {
        Address address;
        elle::Buffer content;

        CryptedBlock(Address address_, elle::Buffer content_) // FIXME: weak ?
          : address(address_)
          , content(std::move(content_))
        {}

        CryptedBlock(elle::serialization::SerializerIn& s)
          : address(Address::null)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("address", this->address);
          s.serialize("content", this->content);
        }
      };

      void
      Paranoid::_store(blocks::Block& block)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        CryptedBlock crypted(block.address(), block.data());
        elle::Buffer raw;
        {
          elle::IOStream output(
            new elle::OutputStreamBuffer<elle::Buffer>(raw));
          elle::serialization::json::SerializerOut serializer(output);
          crypted.serialize(serializer);
        }
        this->_storage->set(block.address(), raw, true, true);
      }

      std::unique_ptr<blocks::Block>
      Paranoid::_fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        elle::Buffer raw;
        try
        {
          raw = this->_storage->get(address);
        }
        catch (infinit::storage::MissingKey const&)
        {
          return nullptr;
        }
        elle::IOStream input(
          new elle::InputStreamBuffer<elle::Buffer>(raw));
        elle::serialization::json::SerializerIn serializer(input);
        CryptedBlock crypted(serializer);
        if (crypted.address != address)
          throw elle::Error(
            elle::sprintf(
              "storage yielded a block with address %s at address %s",
              crypted.address, address));
        return elle::make_unique<blocks::Block>(
          crypted.address, std::move(crypted.content));
      }

      void
      Paranoid::_remove(Address address)
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
