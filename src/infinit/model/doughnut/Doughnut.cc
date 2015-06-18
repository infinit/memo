#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Error.hh>
#include <elle/log.hh>

#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Remote.hh>


ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      struct OKBContent
      {
      // Construction
      public:
        OKBContent(cryptography::KeyPair const& keys)
          : _key()
          , _owner(keys.K())
          , _version(0)
          , _signature()
        {
          auto block_keys = cryptography::KeyPair::generate
            (cryptography::Cryptosystem::rsa, 2048);
          this->_owner._signature = block_keys.k().sign(keys.K());
          this->_key.~PublicKey();
          new (&this->_key) cryptography::PublicKey(block_keys.K());
        }

        OKBContent()
          : _key()
          , _owner()
          , _version()
          , _signature()
        {}

      // Content
      public:
        struct Owner
        {
          Owner(cryptography::PublicKey key)
            : _key(std::move(key))
            , _signature()
          {}

          Owner()
            : _key()
            , _signature()
          {}

          ELLE_ATTRIBUTE_R(cryptography::PublicKey, key);
          ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
          friend class OKBContent;

          void
          serialize(elle::serialization::Serializer& input)
          {
            input.serialize("key", this->_key);
            input.serialize("signature", this->_signature);
          }
        };
        ELLE_ATTRIBUTE_R(cryptography::PublicKey, key);
        ELLE_ATTRIBUTE_R(Owner, owner);
        ELLE_ATTRIBUTE_R(int, version);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
        friend class OKB;
      };

      class OKB
        : public OKBContent
        , public blocks::MutableBlock
      {
      // Types
      public:
        typedef OKB Self;
        typedef blocks::MutableBlock Super;


      // Construction
      public:
        OKB(cryptography::KeyPair const& keys)
          : OKBContent(keys)
          , Super(
              Address(
                cryptography::oneway::hash
                (this->key(), cryptography::oneway::Algorithm::sha256)
                .buffer().contents()))
        {}

      // Serialization
      public:
        OKB(elle::serialization::Serializer& input)
          : OKBContent()
          , Super(input)
        {
          this->_serialize(input);
        }

        virtual
        void
        serialize(elle::serialization::Serializer& s) override
        {
          this->Super::serialize(s);
          this->_serialize(s);
        }

      private:
        void
        _serialize(elle::serialization::Serializer& input)
        {
          input.serialize("owner", this->_owner);
          input.serialize("version", this->_version);
          input.serialize("signature", this->_signature);
        }
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_serialization("OKB");

      Doughnut::Doughnut(cryptography::KeyPair keys,
                         std::unique_ptr<overlay::Overlay> overlay)
        : _overlay(std::move(overlay))
        , _keys(keys)
      {}

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create block", *this);
        return elle::make_unique<OKB>(this->_keys);
      }

      void
      Doughnut::_store(blocks::Block& block)
      {
        this->_owner(block.address())->store(block);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        return this->_owner(address)->fetch(address);
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_owner(address)->remove(address);
      }

      std::unique_ptr<Peer>
      Doughnut::_owner(Address const& address) const
      {
        return elle::make_unique<Remote>(this->_overlay->lookup(address));
      }
    }
  }
}
