#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
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

      template <typename Block>
      BaseOKB<Block>::BaseOKB(Doughnut* owner)
        : OKBHeader(owner->keys())
        , Super(this->_hash_address())
        , _version(-1)
        , _signature()
        , _doughnut(owner)
      {}

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
        s.serialize("data", this->data());
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal()
      {
        ++this->_version; // FIXME: idempotence in case the write fails ?
        auto sign = this->_sign();
        this->_signature =
          this->_doughnut->keys().k().sign(cryptography::Plain(sign));
        ELLE_DUMP("%s: sign %s with %s: %f",
                  *this, sign, this->_doughnut->keys().k(), this->_signature);
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_validate(blocks::Block const& previous) const
      {
        if (!this->_validate())
          return false;
        if (!this->_validate_version<BaseOKB<Block>>
            (previous, &BaseOKB<Block>::_version, this->version()))
          return false;
        return true;
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_validate() const
      {
        if (!static_cast<OKBHeader const*>(this)->validate(this->address()))
          return false;
        auto sign = this->_sign();
        if (!this->_check_signature
            (this->_owner_key, this->_signature, sign, "owner"))
          return false;
        return true;
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_check_signature(cryptography::PublicKey const& key,
                            cryptography::Signature const& signature,
                            elle::Buffer const& data,
                            std::string const& name) const
      {
        ELLE_DUMP("%s: check %f signs %s with %s",
                  *this, signature, data, key);
        if (!key.verify(signature, cryptography::Plain(data)))
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
      BaseOKB<Block>::BaseOKB(elle::serialization::Serializer& input)
        : OKBHeader()
        , Super(input)
        , _version(-1)
        , _signature()
        , _doughnut(nullptr)
      {
        this->_serialize(input);
      }

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
