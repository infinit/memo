#include <infinit/model/doughnut/UB.hh>

#include <elle/log.hh>
#include <elle/utils.hh>

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

      UB::UB(Doughnut* dn, std::string name, cryptography::rsa::PublicKey key, bool reverse)
        : Super(reverse ? UB::hash_address(key, dn->version()) : UB::hash_address(name, dn->version()))
        , _name(std::move(name))
        , _key(std::move(key))
        , _reverse(reverse)
        , _doughnut(dn)
      {}

      UB::UB(UB const& other)
        : Super(other)
        , _name{other._name}
        , _key{other._key}
        , _reverse{other._reverse}
        , _doughnut(other._doughnut)
      {}

      Address
      UB::hash_address(std::string const& name, elle::Version const& version)
      {
        auto hash = cryptography::hash (elle::sprintf("UB/%s", name),
                                        cryptography::Oneway::sha256);
        return version >= elle::Version(0, 5, 0)
          ? Address(hash.contents(), flags::immutable_block)
          : Address(hash.contents());
      }

      Address
      UB::hash_address(cryptography::rsa::PublicKey const& key,
                       elle::Version const& version)
      {
        auto buf = cryptography::rsa::publickey::der::encode(key);
        auto hash = cryptography::hash (elle::sprintf("RUB/%s", buf),
                                        cryptography::Oneway::sha256);
        return version >= elle::Version(0, 5, 0)
          ? Address(hash.contents(), flags::immutable_block)
          : Address(hash.contents());
      }

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<blocks::Block>
      UB::clone() const
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
          UB::hash_address(this->key(), this->_doughnut->version())
          : UB::hash_address(this->name(), this->_doughnut->version());

        if (this->address() != expected_address
          && this->address() != expected_address.unflagged())
        {
          auto reason = elle::sprintf("address %x invalid, expecting %x",
                                      this->address(), expected_address);
          ELLE_DUMP("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        if (this->_doughnut->version() >= elle::Version(0, 5, 0))
          elle::unconst(this)->_address = expected_address; // upgrade from unmasked if required
        return blocks::ValidationResult::success();
      }

      blocks::RemoveSignature
      UB::_sign_remove() const
      {
        auto& keys = this->_doughnut->keys();
        if (keys.K() != this->_key && keys.K() != *this->_doughnut->owner())
          throw elle::Error("Only block owner and network owner can delete UB");
        auto to_sign = elle::serialization::binary::serialize((Block*)elle::unconst(this));
        auto signature = keys.k().sign(to_sign);
        blocks::RemoveSignature res;
        res.signature_key.emplace(keys.K());
        res.signature.emplace(signature);
        return res;
      }

      blocks::ValidationResult
      UB::_validate_remove(blocks::RemoveSignature const& sig) const
      {
        if (!sig.signature_key || !sig.signature)
          return blocks::ValidationResult::failure("Missing key or signature");
        auto to_sign = elle::serialization::binary::serialize((Block*)elle::unconst(this));
        bool ok = sig.signature_key->verify(*sig.signature, to_sign);
        if (!ok)
          return blocks::ValidationResult::failure("Invalid signature");
        if (*sig.signature_key != *this->_doughnut->owner()
          && *sig.signature_key != this->_key)
          return blocks::ValidationResult::failure("Unauthorized signing key");
        return blocks::ValidationResult::success();
      }

      blocks::ValidationResult
      UB::_validate(const Block& new_block) const
      {
        auto ub = dynamic_cast<const UB*>(&new_block);
        if (ub)
        {
          if (this->_name == ub->_name
            && this->_key == ub->_key
            && this->_reverse == ub->_reverse)
          return blocks::ValidationResult::success();
        }
        return blocks::ValidationResult::failure("UB overwrite denied");
      }

      /*--------------.
      | Serialization |
      `--------------*/

      UB::UB(elle::serialization::SerializerIn& input,
             elle::Version const& version)
        : Super(input, version)
        , _name(input.deserialize<std::string>("name"))
        , _key(input.deserialize<cryptography::rsa::PublicKey>("key"))
        , _reverse(input.deserialize<bool>("reverse"))
      {
        input.serialize_context<Doughnut*>(this->_doughnut);
      }

      void
      UB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s,version);
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
      Register<UB> _register_ub_serialization("UB");
    }
  }
}
