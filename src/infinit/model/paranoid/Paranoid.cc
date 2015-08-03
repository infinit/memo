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
      Paranoid::Paranoid(infinit::cryptography::rsa::KeyPair keys,
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
      Paranoid::_store(blocks::Block& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        CryptedBlock crypted(block.address(), block.data());
        elle::Buffer raw;
        {
          elle::IOStream output(raw.ostreambuf());
          elle::serialization::json::SerializerOut serializer(output, version);
          serializer.serialize_forward(crypted);
        }
        this->_storage->set(
          block.address(),
          this->_keys.K().seal(raw),
          mode == STORE_ANY || mode == STORE_INSERT,
          mode == STORE_ANY || mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Paranoid::_fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        elle::Buffer raw;
        try
        {
          auto stored = this->_storage->get(address);
          raw = std::move(this->_keys.k().open(stored));
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

      struct ParanoidModelConfig:
        public ModelConfig
      {
      public:
        // boost::optional does not support in-place construction, use a
        // std::unique_ptr instead since KeyPair is not copiable.
        std::unique_ptr<infinit::cryptography::rsa::KeyPair> keys;
        std::unique_ptr<infinit::storage::StorageConfig> storage;

        ParanoidModelConfig(elle::serialization::SerializerIn& input)
          : ModelConfig()
        {
          this->serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("keys", this->keys);
          s.serialize("storage", this->storage);
        }

        virtual
        std::unique_ptr<infinit::model::Model>
        make(std::vector<std::string> const&, bool, bool)
        {
          if (!this->keys)
          {
            this->keys.reset(
              new infinit::cryptography::rsa::KeyPair(
                infinit::cryptography::rsa::keypair::generate(2048)));
            elle::serialization::json::SerializerOut output(std::cout);
            std::cout << "No key specified, generating fresh ones:" << std::endl;
            this->keys->serialize(output);
          }
          return elle::make_unique<infinit::model::paranoid::Paranoid>
            (std::move(*this->keys), this->storage->make());
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<ParanoidModelConfig> _register_ParanoidModelConfig("paranoid");
    }
  }
}
