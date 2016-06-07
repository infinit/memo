#include <infinit/model/doughnut/OKB.hh>

#include <elle/bench.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>
#include <elle/utility/Move.hh>

#include <cryptography/hash.hh>
#include <cryptography/random.hh>

#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.OKB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      OKBHeader::OKBHeader(Doughnut* dht,
                           cryptography::rsa::KeyPair const& keys,
                           boost::optional<elle::Buffer> salt)
        : _dht(dht)
        , _owner_key(keys.public_key())
        , _signature()
        , _doughnut(dht)
      {
        if (salt)
          this->_salt = std::move(salt.get());
        else
        {
          this->_salt = cryptography::random::generate<elle::Buffer>(24);
          uint64_t now = (boost::posix_time::microsec_clock::universal_time()
            - boost::posix_time::ptime(boost::posix_time::min_date_time))
            .total_milliseconds();
          _salt.append(&now, 8);
        }
        auto owner_key_buffer =
          elle::serialization::json::serialize(
            *this->_owner_key, elle::Version(0,0,0));
        owner_key_buffer.append(_salt.contents(), _salt.size());
        this->_signature = keys.k().sign(owner_key_buffer);
      }

      OKBHeader::OKBHeader(OKBHeader const& other)
        : _dht(other._dht)
        , _salt(other._salt)
        , _owner_key(other._owner_key)
        , _signature(other._signature)
        , _doughnut(other._doughnut)
      {}

      Address
      OKBHeader::hash_address(Doughnut const& dht,
                              cryptography::rsa::PublicKey const& key,
                              elle::Buffer const& salt)
      {
        return hash_address(key, salt, dht.version());
      }

      Address
      OKBHeader::hash_address(cryptography::rsa::PublicKey const& key,
                              elle::Buffer const& salt,
                              elle::Version const& compatibility_version)
      {
        auto key_buffer = elle::serialization::json::serialize(
          key, elle::Version(0,0,0));
        key_buffer.append(salt.contents(), salt.size());
        auto hash =
          cryptography::hash(key_buffer, cryptography::Oneway::sha256);
        return Address(hash.contents(), flags::mutable_block,
                       compatibility_version >= elle::Version(0, 5, 0));
      }

      Address
      OKBHeader::_hash_address() const
      {
        return hash_address(*this->_dht, *this->_owner_key, this->_salt);
      }

      blocks::ValidationResult
      OKBHeader::validate(Address const& address) const
      {
        static elle::Bench bench("bench.okb.validate", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        Address expected_address;
        ELLE_DEBUG("%s: check address", *this)
        {
          expected_address = this->_hash_address();
          if (!equal_unflagged(address, expected_address))
          {
            auto reason = elle::sprintf("address %x invalid, expecting %x",
                                        address, expected_address);
            ELLE_DEBUG("%s: %s", *this, reason);
            return blocks::ValidationResult::failure(reason);
          }
        }
        ELLE_DEBUG("%s: check owner key", *this)
        {
          auto owner_key_buffer = elle::serialization::json::serialize(
            *this->_owner_key, elle::Version(0, 0, 0));
          owner_key_buffer.append(_salt.contents(), _salt.size());
          if (!this->_owner_key->verify(
                this->OKBHeader::_signature, owner_key_buffer))
          {
            ELLE_DEBUG("%s: invalid owner key", *this);
            return blocks::ValidationResult::failure("invalid owner key");
          }
        }
        return blocks::ValidationResult::success();
      }

      OKBHeader::OKBHeader(elle::serialization::SerializerIn& s,
                           elle::Version const&)
        : _salt()
        , _owner_key(std::make_shared(
                       s.deserialize<cryptography::rsa::PublicKey>("key")))
        , _signature()
      {
        s.serialize_context<Doughnut*>(this->_dht);
        s.serialize("owner", *this);
      }

      void
      OKBHeader::serialize(elle::serialization::Serializer& input)
      {
        input.serialize("salt", this->_salt);
        input.serialize("signature", this->_signature);
      }

      /*-------------.
      | Construction |
      `-------------*/

      template <typename Block>
      BaseOKB<Block>::BaseOKB(Doughnut* owner,
                              elle::Buffer data,
                              boost::optional<elle::Buffer> salt)
        : BaseOKB(owner, std::move(data), std::move(salt), owner->keys())
      {}

      template <typename Block>
      BaseOKB<Block>::BaseOKB(Doughnut* owner,
                              elle::Buffer data,
                              boost::optional<elle::Buffer> salt,
                              cryptography::rsa::KeyPair const& owner_keys)
        : BaseOKB(OKBHeader(owner, owner_keys, std::move(salt)),
                  std::move(data), owner_keys.private_key())
      {}

      template <typename Block>
      BaseOKB<Block>::BaseOKB(
        OKBHeader header,
        elle::Buffer data,
        std::shared_ptr<cryptography::rsa::PrivateKey> owner_key)
        : Super(header._hash_address())
        , OKBHeader(std::move(header))
        , _version(-1)
        , _signature()
        , _owner_private_key(std::move(owner_key))
        , _data_plain()
        , _data_decrypted(true)
      {
        this->data(std::move(data));
      }

      template <typename Block>
      BaseOKB<Block>::BaseOKB(BaseOKB<Block> const& other)
        : Super(other)
        , OKBHeader(other)
        , _version{other._version}
        , _signature(other._signature)
        , _owner_private_key(other._owner_private_key)
        , _data_plain{other._data_plain}
        , _data_decrypted{other._data_decrypted}
      {}

      /*--------.
      | Content |
      `--------*/

      template <typename Block>
      bool
      BaseOKB<Block>::operator ==(blocks::Block const& other) const
      {
        auto other_okb = dynamic_cast<Self const*>(&other);
        if (!other_okb)
          return false;
        if (this->_salt != other_okb->_salt)
          return false;
        if (*this->_owner_key != *other_okb->_owner_key)
          return false;
        //if (this->_signature->value() != other_okb->_signature->value())
        //  return false;
        return this->Super::operator ==(other);
      }

      template <typename Block>
      int
      BaseOKB<Block>::version() const
      {
        return this->_version;
      }

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
        this->_validated = false;
      }

      template <typename Block>
      void
      BaseOKB<Block>::data(std::function<void (elle::Buffer&)> transformation)
      {
        this->_decrypt_data();
        transformation(this->_data_plain);
        this->_data_changed = true;
        this->_validated = false;
      }

      template <typename Block>
      void
      BaseOKB<Block>::_decrypt_data() const
      {
        if (!this->_data_decrypted)
        {
          static elle::Bench bench("bench.decrypt", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ELLE_TRACE_SCOPE("%s: decrypt data", *this);
          const_cast<BaseOKB<Block>*>(this)->_data_plain =
            this->_decrypt_data(this->_data);
          ELLE_DUMP("%s: decrypted data: %s", *this, this->_data_plain);
          const_cast<BaseOKB<Block>*>(this)->_data_decrypted = true;
        }
        else
          ELLE_DEBUG("Data already decrypted");
      }

      template <typename Block>
      std::unique_ptr<typename BaseOKB<Block>::OwnerSignature>
      BaseOKB<Block>::_sign() const
      {
        return elle::make_unique<OwnerSignature>(*this);
      }

      template <typename Block>
      elle::Buffer
      BaseOKB<Block>::_decrypt_data(elle::Buffer const& data) const
      {
        if (!this->_owner_private_key)
          throw elle::Error("attempting to decrypt an unowned OKB");
        return this->_owner_private_key->open(data);
      }

      /*-----------.
      | Validation |
      `-----------*/

      template <typename Block>
      BaseOKB<Block>::OwnerSignature::OwnerSignature(
        BaseOKB<Block> const& block)
        : _block(block)
      {}

      template <typename Block>
      void
      BaseOKB<Block>::OwnerSignature::serialize(
        elle::serialization::Serializer& s_,
        elle::Version const& v)
      {
        // FIXME: Improve when split-serialization is added.
        ELLE_ASSERT(s_.out());
        auto& s = reinterpret_cast<elle::serialization::SerializerOut&>(s_);
        s.serialize("salt", this->_block.salt());
        s.serialize("owner_key", *this->_block.owner_key());
        s.serialize("version", this->_block._version);
        this->_serialize(s, v);
      }

      template <typename Block>
      void
      BaseOKB<Block>::OwnerSignature::_serialize(
        elle::serialization::SerializerOut& s,
        elle::Version const& v)
      {
        s.serialize("data", this->_block.blocks::Block::data());
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal(boost::optional<int> version)
      {
        if (this->_data_changed)
        {
          ELLE_DEBUG_SCOPE("%s: data changed, seal", *this);
          ELLE_DUMP("%s: data: %s", *this, this->_data_plain);
          auto encrypted =
            this->doughnut()->keys().K().seal(this->_data_plain);
          ELLE_DUMP("%s: encrypted data: %s", *this, encrypted);
          this->Block::data(std::move(encrypted));
          this->_seal_okb(version);
          this->_data_changed = false;
        }
        else if (version)
          this->_seal_okb(version);
        else
          ELLE_DEBUG("%s: data didn't change", *this);
      }

      template <typename T>
      static
      void
      null_deleter(T*)
      {}

      template <typename Block>
      void
      BaseOKB<Block>::_seal_okb(boost::optional<int> version, bool bump_version)
      {
        if (version)
          this->_version = *version;
        else
          if (bump_version)
            ++this->_version; // FIXME: idempotence in case the write fails ?
        if (!this->_owner_private_key)
          throw elle::Error("attempting to seal an unowned OKB");
        this->_signature =
          std::make_shared<SignFuture>(
            this->_owner_private_key->sign_async(*this->_sign(),
              this->doughnut()->version()));
      }

      template <typename Block>
      elle::Buffer const&
      BaseOKB<Block>::signature() const
      {
        return this->_signature->value();
      }

      template <typename Block>
      blocks::ValidationResult
      BaseOKB<Block>::_validate(Model const& model) const
      {
        static elle::Bench bench("bench.okb._validate", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        if (auto res = static_cast<OKBHeader const*>
            (this)->validate(this->address())); else
          return res;
        ELLE_DEBUG("%s: check signature", *this)
        {
          ELLE_ASSERT(this->signature() != elle::Buffer());
          auto sign = this->_sign();
          if (!this->_owner_key->verify(this->signature(), *sign))
          {
            ELLE_TRACE("%s: invalid signature for version %s: '%x'",
              *this, this->_version, this->signature());
            return blocks::ValidationResult::failure("invalid signature");
          }
        }
        // Upgrade from unmasked address if required, *after* checking signature
        /*
        if (this->_doughnut->version() >= elle::Version(0, 5, 0))
          elle::unconst(this)->_address = this->_hash_address();
          */
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
      BaseOKB<Block>::BaseOKB(elle::serialization::SerializerIn& s,
                              elle::Version const& version)
        : Super(s, version)
        , OKBHeader(s, version)
        , _owner_private_key()
        , _data_plain()
        , _data_decrypted(false)
      {
        this->_serialize(s, version);
        if (this->doughnut() &&
            *this->owner_key() == this->doughnut()->keys().K())
          this->_owner_private_key = this->doughnut()->keys().private_key();
      }

      template <typename Block>
      void
      BaseOKB<Block>::serialize(elle::serialization::Serializer& s,
                                elle::Version const& version)
      {
        this->Super::serialize(s, version);
        s.serialize("key", *this->_owner_key);
        s.serialize("owner", static_cast<OKBHeader&>(*this));
        this->_serialize(s, version);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_serialize(elle::serialization::Serializer& s,
                                 elle::Version const& version)
      {
        s.serialize_context<Doughnut*>(this->_doughnut);
        s.serialize("version", this->_version);
        if (!this->_signature)
          this->_signature = std::make_shared<SignFuture>();
        if (version < elle::Version(0, 4, 0))
          if (s.out())
          {
            auto value = elle::WeakBuffer(this->_signature->value()).range(4);
            s.serialize("signature", value);
          }
          else
          {
            elle::Buffer signature;
            s.serialize("signature", signature);
            auto versioned =
              elle::serialization::binary::serialize(elle::Version(0, 3, 0));
            versioned.size(versioned.size() + signature.size());
            memcpy(versioned.mutable_contents() + 4,
                   signature.contents(), signature.size());
            this->_signature = std::make_shared<SignFuture>(std::move(versioned));
          }
        else
        {
          bool need_signature = !s.context().has<OKBDontWaitForSignature>();
          if (!need_signature)
          {
            if (s.out())
            {
              bool ready = !this->_signature->running();
              s.serialize("signature_ready", ready);
              if (ready)
                s.serialize("signature", this->_signature->value());
              else
              {
                bool is_doughnut_key = !this->owner_private_key() ||
                  *this->owner_private_key() == this->doughnut()->keys_shared()->k();
                s.serialize("signature_is_doughnut_key", is_doughnut_key);
                if (!is_doughnut_key)
                  s.serialize("signature_key", this->_owner_private_key);
              }
            }
            else
            {
              bool ready;
              s.serialize("signature_ready", ready);
              if (ready)
                s.serialize("signature", this->_signature->value());
              else
              {
                bool is_doughnut_key;
                s.serialize("signature_is_doughnut_key", is_doughnut_key);
                if (!is_doughnut_key)
                  s.serialize("signature_key", this->_owner_private_key);
                else
                  this->_owner_private_key = this->doughnut()->keys_shared()->private_key();
                // can't restart signing now, we might be in OKB ctor
              }
            }
          }
          else
          {
            s.serialize("signature", this->_signature->value());
          }
        }
      }

      template
      class BaseOKB<blocks::MutableBlock>;
      template
      class BaseOKB<blocks::ACLBlock>;
      template
      class BaseOKB<blocks::GroupBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
      static const elle::TypeInfo::RegisterAbbrevation
      _okb_abbr("BaseOKB<infinit::model::blocks::MutableBlock>", "OKB");
    }
  }
}
