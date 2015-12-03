#include <infinit/model/doughnut/UB.hh>

#include <elle/log.hh>

#include <cryptography/hash.hh>
#include <cryptography/rsa/KeyPair.hh>

#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.UB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      UB::UB(std::string name, cryptography::rsa::PublicKey key, bool reverse)
        : Super(reverse ? UB::hash_address(key) : UB::hash_address(name))
        , _name(std::move(name))
        , _key(std::move(key))
        , _reverse(reverse)
      {}

      UB::UB(UB const& other)
        : Super(other)
        , _name{other._name}
        , _key{other._key}
        , _reverse{other._reverse}
      {}

      Address
      UB::hash_address(std::string const& name)
      {
        auto hash = cryptography::hash (elle::sprintf("UB/%s", name),
                                        cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      Address
      UB::hash_address(cryptography::rsa::PublicKey const& key)
      {
        auto buf = cryptography::rsa::publickey::der::encode(key);
        auto hash = cryptography::hash (elle::sprintf("RUB/%s", buf),
                                        cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<blocks::Block>
      UB::clone(bool) const
      {
        return std::unique_ptr<blocks::Block>(new UB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      UB::_seal()
      {}

      // FIXME: factor with CHB
      blocks::ValidationResult
      UB::_validate() const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        auto expected_address = this->reverse() ?
          UB::hash_address(this->key())
          : UB::hash_address(this->name());

        if (this->address() != expected_address)
        {
          auto reason = elle::sprintf("address %x invalid, expecting %x",
                                      this->address(), expected_address);
          ELLE_DUMP("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        return blocks::ValidationResult::success();
      }

      /*--------------.
      | Serialization |
      `--------------*/

      UB::UB(elle::serialization::SerializerIn& input)
        : Super(input)
        , _name(input.deserialize<std::string>("name"))
        , _key(input.deserialize<cryptography::rsa::PublicKey>("key"))
        , _reverse(input.deserialize<bool>("reverse"))
      {}

      void
      UB::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        this->_serialize(s);
      }

      void
      UB::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", this->_name);
        s.serialize("key", this->_key);
        s.serialize("reverse", this->_reverse);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<UB> _register_chb_serialization("UB");
    }
  }
}
