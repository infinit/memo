#include <infinit/model/doughnut/UB.hh>

#include <elle/log.hh>

#include <cryptography/hash.hh>
#include <cryptography/rsa/KeyPair.hh>

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

      UB::UB(std::string name, cryptography::rsa::PublicKey key)
        : Super(UB::_hash_address(name))
        , _name(std::move(name))
        , _key(std::move(key))
      {}

      /*-----------.
      | Validation |
      `-----------*/

      void
      UB::_seal()
      {}

      // FIXME: factor with CHB
      bool
      UB::_validate() const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        auto expected_address = UB::_hash_address(this->name());
        if (this->address() != expected_address)
        {
          ELLE_DUMP("%s: address %x invalid, expecting %x",
                    *this, this->address(), expected_address);
          return false;
        }
        return true;
      }

      /*--------------.
      | Serialization |
      `--------------*/

      static auto dummy_keys = cryptography::rsa::keypair::generate(2048);
      UB::UB(elle::serialization::Serializer& input)
        : Super(input)
        , _name()
        , _key(dummy_keys.K())
      {
        this->_serialize(input);
      }

      void
      UB::serialize(elle::serialization::Serializer& s)
      {
        this->_serialize(s);
      }

      void
      UB::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", this->_name);
        s.serialize("key", this->_key);
      }

      /*--------.
      | Details |
      `--------*/

      Address
      UB::_hash_address(std::string const& name)
      {
        auto hash = cryptography::hash (elle::sprintf("UB/%s", name),
                                        cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<UB> _register_chb_serialization("UB");
    }
  }
}
