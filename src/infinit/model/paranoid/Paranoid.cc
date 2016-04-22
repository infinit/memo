#include <infinit/model/paranoid/Paranoid.hh>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.paranoid.Paranoid");

namespace infinit
{
  namespace model
  {
    namespace paranoid
    {
      Paranoid::Paranoid(infinit::cryptography::rsa::KeyPair keys,
                         std::unique_ptr<storage::Storage> storage,
                         elle::Version version)
        : Model(std::move(version))
        , _keys(std::move(keys))
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
      Paranoid::_store(std::unique_ptr<blocks::Block> block,
                       StoreMode mode,
                       std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, *block);
        CryptedBlock crypted(block->address(), block->data());
        elle::Buffer raw;
        {
          elle::IOStream output(raw.ostreambuf());
          elle::serialization::json::SerializerOut serializer(output);
          serializer.serialize_forward(crypted);
        }
        this->_storage->set(
          block->address(),
          this->_keys.K().seal(raw),
          mode == STORE_INSERT,
          mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Paranoid::_fetch(Address address,
                       boost::optional<int>) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        elle::Buffer raw;
        try
        {
          auto stored = this->_storage->get(address);
          raw = this->_keys.k().open(stored);
        }
        catch (infinit::storage::MissingKey const&)
        {
          throw MissingBlock(address);
        }
        elle::IOStream input(raw.istreambuf());
        elle::serialization::json::SerializerIn serializer(input);
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
      Paranoid::_remove(Address address, blocks::RemoveSignature rs)
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

        ParanoidModelConfig(elle::serialization::SerializerIn& input)
          : ModelConfig(input)
        {
          this->_serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          ModelConfig::serialize(s);
          this->_serialize(s);
        }

        void
        _serialize(elle::serialization::Serializer& s)
        {
          s.serialize("keys", this->keys);
        }

        virtual
        std::unique_ptr<infinit::model::Model>
        make(overlay::NodeEndpoints const&,
             bool,
             boost::filesystem::path const&)
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
          return elle::make_unique<infinit::model::paranoid::Paranoid>(
            std::move(*this->keys), this->storage->make(), this->version);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<ParanoidModelConfig> _register_ParanoidModelConfig("paranoid");
    }
  }
}
