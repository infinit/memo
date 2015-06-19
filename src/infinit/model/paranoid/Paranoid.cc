#include <infinit/model/paranoid/Paranoid.hh>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/storage/MissingKey.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit.model.paranoid.Paranoid");

static elle::Version const version
  (INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);

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
          s.serialize_forward(*this);
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
          elle::IOStream output(raw.ostreambuf());
          elle::serialization::json::SerializerOut serializer(output, version);
          serializer.serialize_forward(crypted);
        }
        this->_storage->set(block.address(),
                            this->_keys.K().encrypt(cryptography::Plain(raw)).buffer(), true, true);
      }

      std::unique_ptr<blocks::Block>
      Paranoid::_fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        elle::Buffer raw;
        try
        {
          raw = std::move(this->_keys.k().decrypt(
            cryptography::Code(this->_storage->get(address))).buffer());
        }
        catch (infinit::storage::MissingKey const&)
        {
          return nullptr;
        }
        elle::IOStream input(raw.istreambuf());
        elle::serialization::json::SerializerIn serializer(input, version);
        CryptedBlock crypted(serializer);
        if (crypted.address != address)
          throw elle::Error(
            elle::sprintf(
              "storage yielded a block with address %s at address %s",
              crypted.address, address));
        return this->_construct_block<blocks::MutableBlock>
          (crypted.address, std::move(crypted.content));
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
