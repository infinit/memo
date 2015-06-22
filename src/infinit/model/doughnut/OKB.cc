#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.OKB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      OKBHeader::OKBHeader(cryptography::KeyPair const& keys)
        : _key()
        , _owner_key(keys.K())
        , _signature()
      {
        auto block_keys = cryptography::KeyPair::generate
          (cryptography::Cryptosystem::rsa, 2048);
        this->_key.~PublicKey();
        new (&this->_key) cryptography::PublicKey(block_keys.K());
        this->_signature = block_keys.k().sign(this->_owner_key);
      }

      OKBHeader::OKBHeader()
        : _key()
        , _owner_key()
        , _signature()
      {}

      /*-------------.
      | Construction |
      `-------------*/

      void
      OKBHeader::serialize(elle::serialization::Serializer& input)
      {
        input.serialize("key", this->_owner_key);
        input.serialize("signature", this->_signature);
      }

      OKB::OKB(cryptography::KeyPair const& keys)
        : OKBHeader(keys)
        , Super(OKB::_hash_address(this->_key))
        , _version(-1)
        , _keys(keys)
      {}

      /*-----------.
      | Validation |
      `-----------*/

      elle::Buffer
      OKB::_sign() const
      {
        elle::Buffer res;
        {
          // FIXME: use binary to sign
          elle::IOStream s(res.ostreambuf());
          elle::serialization::json::SerializerOut output(s, false);
          output.serialize("block_key", this->_key);
          output.serialize("data", this->data());
          output.serialize("version", this->_version);
        }
        return res;
      }

      void
      OKB::_seal()
      {
        ELLE_DEBUG_SCOPE("%s: seal", *this);
        ++this->_version; // FIXME: idempotence in case the write fails ?
        auto sign = this->_sign();
        this->_signature = this->_keys.k().sign(cryptography::Plain(sign));
        ELLE_DUMP("%s: sign %s with %s: %f",
                  *this, sign, this->_keys.k(), this->_signature);
      }

      bool
      OKB::_validate(blocks::Block const& previous) const
      {
        if (!this->_validate())
          return false;
        auto previous_okb = dynamic_cast<OKB const*>(&previous);
        return previous_okb && this->version() > previous_okb->version();
      }

      bool
      OKB::_validate() const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        auto expected_address = OKB::_hash_address(this->_key);
        if (this->address() != expected_address)
        {
          ELLE_DUMP("%s: address %x invalid, expecting %x",
                    *this, this->address(), expected_address);
          return false;
        }
        else
          ELLE_DUMP("%s: address is valid", *this);
        if (!this->_key.verify(this->OKBHeader::_signature, this->_owner_key))
        {
          return false;
        }
        else
          ELLE_DUMP("%s: owner key is valid", *this);
        auto sign = this->_sign();
        if (!this->_owner_key.verify(this->_signature,
                                     cryptography::Plain(sign)))
          return false;
        else
          ELLE_DUMP("%s: payload is valid", *this);
        return true;
      }

      Address
      OKB::_hash_address(cryptography::PublicKey const& key)
      {
        auto hash = cryptography::oneway::hash
          (key, cryptography::oneway::Algorithm::sha256);
        return Address(hash.buffer().contents());
      }

      /*--------------.
      | Serialization |
      `--------------*/

      OKB::OKB(elle::serialization::Serializer& input)
        : OKBHeader()
        , Super(input)
      {
        this->_serialize(input);
      }

      void
      OKB::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      void
      OKB::_serialize(elle::serialization::Serializer& input)
      {
        input.serialize("key", this->_key);
        input.serialize("owner", static_cast<OKBHeader&>(*this));
        input.serialize("version", this->_version);
        input.serialize("signature", this->_signature);
      }
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
    }
  }
}
