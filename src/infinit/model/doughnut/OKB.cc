#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Doughnut.hh>

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

      Address
      OKBHeader::_hash_address() const
      {
        auto hash = cryptography::oneway::hash
          (this->_key, cryptography::oneway::Algorithm::sha256);
        return Address(hash.buffer().contents());
      }

      bool
      OKBHeader::validate(Address const& address) const
      {
        auto expected_address = this->_hash_address();
        if (address != expected_address)
        {
          ELLE_DUMP("%s: address %x invalid, expecting %x",
                    *this, address, expected_address);
          return false;
        }
        else
          ELLE_DUMP("%s: address is valid", *this);
        if (!this->_key.verify(this->OKBHeader::_signature, this->_owner_key))
          return false;
        else
          ELLE_DUMP("%s: owner key is valid", *this);
        return true;
      }

      void
      OKBHeader::serialize(elle::serialization::Serializer& input)
      {
        input.serialize("key", this->_owner_key);
        input.serialize("signature", this->_signature);
      }

      /*-------------.
      | Construction |
      `-------------*/

      OKB::OKB(Doughnut* owner)
        : OKBHeader(owner->keys())
        , Super(this->_hash_address())
        , _version(-1)
        , _signature()
        , _doughnut(owner)
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
          this->_sign(output);
        }
        return res;
      }

      void
      OKB::_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize("block_key", this->_key);
        s.serialize("data", this->data());
        s.serialize("version", this->_version);
      }

      void
      OKB::_seal()
      {
        ++this->_version; // FIXME: idempotence in case the write fails ?
        auto sign = this->_sign();
        this->_signature =
          this->_doughnut->keys().k().sign(cryptography::Plain(sign));
        ELLE_DUMP("%s: sign %s with %s: %f",
                  *this, sign, this->_doughnut->keys().k(), this->_signature);
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
        if (!static_cast<OKBHeader const*>(this)->validate(this->address()))
          return false;
        auto sign = this->_sign();
        ELLE_DUMP("%s: check %f signs %s with %s",
                  *this, this->_signature, sign, this->_owner_key)
          if (!this->_owner_key.verify(this->_signature,
                                       cryptography::Plain(sign)))
          {
            ELLE_TRACE("%s: data signature is invalid", *this);
            return false;
          }
          else
            ELLE_DUMP("%s: data signature is valid", *this);
        return true;
      }

      /*--------------.
      | Serialization |
      `--------------*/

      OKB::OKB(elle::serialization::Serializer& input)
        : OKBHeader()
        , Super(input)
        , _version(-1)
        , _signature()
        , _doughnut(nullptr)
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
      OKB::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("key", this->_key);
        s.serialize("owner", static_cast<OKBHeader&>(*this));
        s.serialize("version", this->_version);
        s.serialize("signature", this->_signature);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
    }
  }
}
