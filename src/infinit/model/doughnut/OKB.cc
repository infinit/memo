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
      OKBHeader::OKBHeader(cryptography::rsa::KeyPair const& keys,
                           boost::optional<elle::Buffer> salt)
        : _owner_key(keys.public_key())
        , _signature()
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
          elle::serialization::json::serialize(this->_owner_key);
        owner_key_buffer.append(_salt.contents(), _salt.size());
        this->_signature = keys.k().sign(owner_key_buffer);
      }

      OKBHeader::OKBHeader(OKBHeader const& other)
        : _salt(other._salt)
        , _owner_key(other._owner_key)
        , _signature(other._signature)
      {}

      Address
      OKBHeader::hash_address(cryptography::rsa::PublicKey const& key,
                              elle::Buffer const& salt)
      {
        auto key_buffer =
          elle::serialization::json::serialize(key);
        key_buffer.append(salt.contents(), salt.size());
        auto hash =
          cryptography::hash(key_buffer, cryptography::Oneway::sha256);
        return Address(hash.contents());
      }
      Address
      OKBHeader::_hash_address() const
      {
        return hash_address(*this->_owner_key, this->_salt);
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
          auto owner_key_buffer =
            elle::serialization::json::serialize(*this->_owner_key);
          owner_key_buffer.append(_salt.contents(), _salt.size());
          if (!this->_owner_key->verify(this->OKBHeader::_signature, owner_key_buffer))
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
        : BaseOKB(OKBHeader(owner_keys, std::move(salt)),
                  owner, std::move(data), owner_keys.private_key())
      {}

      template <typename Block>
      BaseOKB<Block>::BaseOKB(
        OKBHeader header,
        Doughnut* owner,
        elle::Buffer data,
        std::shared_ptr<cryptography::rsa::PrivateKey> owner_key)
        : Super(header._hash_address())
        , OKBHeader(std::move(header))
        , _version(-1)
        , _signature()
        , _doughnut(owner)
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
        , _signature(other._signature.value())
        , _doughnut{other._doughnut}
        , _owner_private_key(other._owner_private_key)
        , _data_plain{other._data_plain}
        , _data_decrypted{other._data_decrypted}
      {}

      /*-------.
      | Clone  |
      `-------*/
      template <typename Block>
      std::unique_ptr<blocks::Block>
      BaseOKB<Block>::clone() const
      {
        return std::unique_ptr<blocks::Block>(new BaseOKB<Block>(*this));
      }

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
        if (this->_signature.value() != other_okb->_signature.value())
          return false;
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
          static elle::Bench bench("bench.decrypt", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ELLE_TRACE_SCOPE("%s: decrypt data", *this);
          const_cast<BaseOKB<Block>*>(this)->_data_plain =
            this->_decrypt_data(this->_data);
          ELLE_DUMP("%s: decrypted data: %s", *this, this->_data_plain);
          const_cast<BaseOKB<Block>*>(this)->_data_decrypted = true;
        }
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

      template <typename T>
      static
      void
      null_deleter(T*)
      {}

      template <typename Block>
      void
      BaseOKB<Block>::_seal_okb(bool bump_version)
      {
        if (bump_version)
          ++this->_version; // FIXME: idempotence in case the write fails ?
        if (!this->_owner_private_key)
          throw elle::Error("attempting to seal an unowned OKB");
        this->_signature =
          this->_owner_private_key->sign(*this->_sign(),
                                         this->doughnut()->version());
        // auto sign = elle::utility::move_on_copy(this->_sign());
        // this->_signature =
        //   [keys, sign]
        //   {
        //     static elle::Bench bench("bench.okb.seal.signing", 10000_sec);
        //     elle::Bench::BenchScope scope(bench);
        //     return keys->k().sign(*sign);
        //   };
      }

      template <typename Block>
      elle::Buffer const&
      BaseOKB<Block>::signature() const
      {
        return this->_signature.value();
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
          if (!this->_owner_key->verify(this->signature(), *sign))
          {
            ELLE_TRACE("%s: invalid signature for version %s: '%x'",
              *this, this->_version, this->signature());
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
      BaseOKB<Block>::BaseOKB(elle::serialization::SerializerIn& s,
                              elle::Version const& version)
        : Super(s, version)
        , OKBHeader(s, version)
        , _doughnut(nullptr)
        , _owner_private_key()
        , _data_plain()
        , _data_decrypted(false)
      {
        this->_serialize(s, version);
        if (*this->owner_key() == this->doughnut()->keys().K())
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
        ELLE_ASSERT(this->_doughnut);
        s.serialize("version", this->_version);
        if (version < elle::Version(0, 4, 0))
          if (s.out())
          {
            auto value = elle::WeakBuffer(this->_signature.value()).range(4);
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
            this->_signature = std::move(versioned);
          }
        else
          s.serialize("signature", this->_signature.value());
      }

      template
      class BaseOKB<blocks::MutableBlock>;
      template
      class BaseOKB<blocks::ACLBlock>;
      template
      class BaseOKB<blocks::GroupBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
    }
  }
}
