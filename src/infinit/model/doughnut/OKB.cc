#include <infinit/model/doughnut/OKB.hh>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <cryptography/hash.hh>
#include <cryptography/rsa/KeyPool.hh>

#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.OKB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      OKBHeader::OKBHeader(cryptography::rsa::KeyPair const& keys,
                           cryptography::rsa::KeyPair const& block_keys)
        : _key(block_keys.K())
        , _owner_key(keys.K())
        , _signature()
      {
        auto owner_key_buffer = elle::serialization::serialize
          <cryptography::rsa::PublicKey, elle::serialization::Json>
          (this->_owner_key);
        this->_signature = block_keys.k().sign(owner_key_buffer);
      }

      Address
      OKBHeader::_hash_address() const
      {
        auto key_buffer = elle::serialization::serialize
          <cryptography::rsa::PublicKey, elle::serialization::Json>(this->_key);
        auto hash =
          cryptography::hash(key_buffer, cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      blocks::ValidationResult
      OKBHeader::validate(Address const& address) const
      {
        ELLE_DEBUG("%s: check address", *this)
        {
          auto expected_address = this->_hash_address();
          if (address != expected_address)
          {
            auto reason = elle::sprintf("address %x invalid, expecting %x",
                                        address, expected_address);
            ELLE_DEBUG("%s: %s", *this, reason);
            return blocks::ValidationResult::failure(reason);
          }
        }
        ELLE_DEBUG("%s: check owner key", *this)
        {
          auto owner_key_buffer = elle::serialization::serialize
            <cryptography::rsa::PublicKey, elle::serialization::Json>
            (this->_owner_key);
          if (!this->_key.verify(this->OKBHeader::_signature, owner_key_buffer))
          {
            ELLE_DEBUG("%s: invalid owner key", *this);
            return blocks::ValidationResult::failure("invalid owner key");
          }
        }
        return blocks::ValidationResult::success();
      }

      OKBHeader::OKBHeader(cryptography::rsa::PublicKey key,
                           cryptography::rsa::PublicKey block_key,
                           elle::Buffer signature)
        : _key(std::move(block_key))
        , _owner_key(std::move(key))
        , _signature(std::move(signature))
      {}

      void
      OKBHeader::serialize(elle::serialization::Serializer& input)
      {
        input.serialize("key", this->_owner_key);
        input.serialize("signature", this->_signature);
      }

      /*-------------.
      | Construction |
      `-------------*/

      static cryptography::rsa::KeyPool& pool_get()
      {
        static cryptography::rsa::KeyPool pool(2048, 10);
        return pool;
      }

      template <typename Block>
      BaseOKB<Block>::BaseOKB(Doughnut* owner)
        : OKBHeader(owner->keys(), pool_get().get())
        , Super(this->_hash_address())
        , _version(-1)
        , _signature()
        , _doughnut(owner)
        , _data_plain()
        , _data_decrypted(true)
      {}

      /*--------.
      | Content |
      `--------*/

      template <typename Block>
      elle::Buffer const&
      BaseOKB<Block>::data() const
      {
        this->_decrypt_data();
        return this->_data_plain;
      }

      template <typename Block>
      void
      BaseOKB<Block>::data(elle::Buffer data)
      {
        this->_data_plain = std::move(data);
        this->_data_changed = true;
        this->_data_decrypted = true;
      }

      template <typename Block>
      void
      BaseOKB<Block>::data(std::function<void (elle::Buffer&)> transformation)
      {
        this->_decrypt_data();
        transformation(this->_data_plain);
        this->_data_changed = true;
      }

      template <typename Block>
      void
      BaseOKB<Block>::_decrypt_data() const
      {
        if (!this->_data_decrypted)
        {
          ELLE_TRACE_SCOPE("%s: decrypt data", *this);
          const_cast<BaseOKB<Block>*>(this)->_data_plain =
            this->_decrypt_data(this->_data);
          ELLE_DUMP("%s: decrypted data: %s", *this, this->_data_plain);
          const_cast<BaseOKB<Block>*>(this)->_data_decrypted = true;
        }
      }

      template <typename Block>
      elle::Buffer
      BaseOKB<Block>::_decrypt_data(elle::Buffer const& data) const
      {
        return this->doughnut()->keys().k().open(data);
      }

      /*-----------.
      | Validation |
      `-----------*/

      template <typename Block>
      elle::Buffer
      BaseOKB<Block>::_sign() const
      {
        elle::Buffer res;
        {
          // FIXME: use binary to sign
          elle::IOStream output(res.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("block_key", this->_key);
          s.serialize("version", this->_version);
          this->_sign(s);
        }
        return res;
      }

      template <typename Block>
      void
      BaseOKB<Block>::_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize("data", this->_data);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal()
      {
        if (this->_data_changed)
        {
          ELLE_DEBUG_SCOPE("%s: data changed, seal", *this);
          ELLE_DUMP("%s: data: %s", *this, this->_data_plain);
          auto encrypted =
            this->doughnut()->keys().K().seal(this->_data_plain);
          ELLE_DUMP("%s: encrypted data: %s", *this, encrypted);
          this->Block::data(std::move(encrypted));
          this->_seal_okb();
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal_okb()
      {
        ++this->_version; // FIXME: idempotence in case the write fails ?
        auto sign = this->_sign();
        this->_signature = this->_doughnut->keys().k().sign(sign);
        ELLE_DUMP("%s: sign %s with %s: %f",
                  *this, sign, this->_doughnut->keys().k(), this->_signature);
      }

      template <typename Block>
      blocks::ValidationResult
      BaseOKB<Block>::_validate(blocks::Block const& previous) const
      {
        if (auto res = this->_validate()); else
          return res;
        ELLE_DEBUG("%s: check version", *this)
          if (!this->_validate_version<BaseOKB<Block>>
              (previous, &BaseOKB<Block>::_version, this->version()))
            return blocks::ValidationResult::failure
              ("version validation failed");
        return blocks::ValidationResult::success();
      }

      template <typename Block>
      blocks::ValidationResult
      BaseOKB<Block>::_validate() const
      {
        if (auto res = static_cast<OKBHeader const*>
            (this)->validate(this->address())); else
          return res;
        ELLE_DEBUG("%s: check signature", *this)
        {
          auto sign = this->_sign();
          if (!this->_check_signature
              (this->_owner_key, this->_signature, sign, "owner"))
          {
            ELLE_DEBUG("%s: invalid signature", *this);
            return blocks::ValidationResult::failure("invalid signature");
          }
        }
        return blocks::ValidationResult::success();
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_check_signature(cryptography::rsa::PublicKey const& key,
                            elle::Buffer const& signature,
                            elle::Buffer const& data,
                            std::string const& name) const
      {
        ELLE_DUMP("%s: check %f signs %s with %s",
                  *this, signature, data, key);
        if (!key.verify(signature, data))
        {
          ELLE_TRACE("%s: %s signature is invalid", *this, name);
          return false;
        }
        else
        {
          ELLE_DUMP("%s: %s signature is valid", *this, name);
          return true;
        }
      }

      /*--------------.
      | Serialization |
      `--------------*/

      template <typename Block>
      struct BaseOKB<Block>::SerializationContent
      {
        struct Header
        {
          Header(elle::serialization::SerializerIn& s)
            : key(s.template deserialize<cryptography::rsa::PublicKey>("key"))
            , signature(s.template deserialize<elle::Buffer>("signature"))
          {}

          cryptography::rsa::PublicKey key;
          elle::Buffer signature;
        };

        SerializationContent(elle::serialization::SerializerIn& s)
          : block(s)
          , key(s.template deserialize<cryptography::rsa::PublicKey>("key"))
          , header(s.template deserialize<Header>("owner"))
          , version(s.template deserialize<int>("version"))
          , signature(s.template deserialize<elle::Buffer>("signature"))
        {}

        Block block;
        cryptography::rsa::PublicKey key;
        Header header;
        int version;
        elle::Buffer signature;
      };

      template <typename Block>
      BaseOKB<Block>::BaseOKB(elle::serialization::SerializerIn& input)
        : BaseOKB(SerializationContent(input))
      {
        input.serialize_context<Doughnut*>(this->_doughnut);
      }

      template <typename Block>
      BaseOKB<Block>::BaseOKB(SerializationContent content)
        : OKBHeader(std::move(content.header.key),
                    std::move(content.key),
                    std::move(content.header.signature))
        , Super(std::move(content.block))
        , _version(std::move(content.version))
        , _signature(std::move(content.signature))
        , _doughnut(nullptr)
        , _data_plain()
        , _data_decrypted(false)
      {}

      template <typename Block>
      void
      BaseOKB<Block>::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize_context<Doughnut*>(this->_doughnut);
        ELLE_ASSERT(this->_doughnut);
        s.serialize("key", this->_key);
        s.serialize("owner", static_cast<OKBHeader&>(*this));
        s.serialize("version", this->_version);
        s.serialize("signature", this->_signature);
      }

      template
      class BaseOKB<blocks::MutableBlock>;
      template
      class BaseOKB<blocks::ACLBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
    }
  }
}
