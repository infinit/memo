#include <infinit/model/paranoid/Paranoid.hh>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.paranoid.Paranoid");

namespace infinit
{
  namespace model
  {
    namespace paranoid
    {
      Paranoid::Paranoid(elle::cryptography::rsa::KeyPair keys,
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
          elle::err("storage yielded a block with address %s at address %s",
                    crypted.address, address);
        return this->_construct_block<blocks::MutableBlock>
          (crypted.address, std::move(crypted.content));
      }

      void
      Paranoid::_insert(std::unique_ptr<blocks::Block> block,
                        std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: insert %f", *this, *block);
        CryptedBlock crypted(block->address(), block->data());
        elle::Buffer raw = elle::serialization::json::serialize(crypted);
        this->_storage->set(
          block->address(),
          this->_keys.K().seal(raw),
          true,
          false);
      }

      void
      Paranoid::_update(std::unique_ptr<blocks::Block> block,
                        std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: update %f", *this, *block);
        CryptedBlock crypted(block->address(), block->data());
        elle::Buffer raw = elle::serialization::json::serialize(crypted);
        this->_storage->set(
          block->address(),
          this->_keys.K().seal(raw),
          false,
          true);
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
        std::unique_ptr<elle::cryptography::rsa::KeyPair> keys;

        ParanoidModelConfig(elle::serialization::SerializerIn& input)
          : ModelConfig(input)
        {
          this->_serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s) override
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
        make(bool,
             boost::filesystem::path const&) override
        {
          if (!this->keys)
          {
            this->keys.reset(
              new elle::cryptography::rsa::KeyPair(
                elle::cryptography::rsa::keypair::generate(2048)));
            elle::serialization::json::SerializerOut output(std::cout);
            std::cout << "No key specified, generating fresh ones:" << std::endl;
            this->keys->serialize(output);
          }
          return std::make_unique<infinit::model::paranoid::Paranoid>(
            std::move(*this->keys), this->storage->make(), this->version);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<ParanoidModelConfig> _register_ParanoidModelConfig("paranoid");
    }
  }
}
