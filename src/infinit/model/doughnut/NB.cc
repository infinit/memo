#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/NB.hh>

#include <elle/log.hh>

#include <cryptography/hash.hh>
#include <cryptography/rsa/KeyPair.hh>

#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.NB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      NB::NB(Doughnut* doughnut,
             infinit::cryptography::rsa::PublicKey owner,
             std::string name,
             elle::Buffer data)
        : Super(NB::address(owner, name), std::move(data))
        , _doughnut(std::move(doughnut))
        , _owner(std::move(owner))
        , _name(std::move(name))
      {}

      NB::NB(Doughnut* doughnut,
             infinit::cryptography::rsa::KeyPair keys,
             std::string name,
             elle::Buffer data)
        : Super(NB::address(keys.K(), name), std::move(data))
        , _doughnut(std::move(doughnut))
        , _keys(keys)
        , _owner(keys.K())
        , _name(std::move(name))
      {}

      NB::NB(NB const& other)
        : Super(other)
        , _doughnut(other._doughnut)
        , _keys(other._keys)
        , _owner(other._owner)
        , _name(other._name)
        , _signature(other._signature)
      {}

      Address
      NB::address(infinit::cryptography::rsa::PublicKey const& owner,
                       std::string const& name)
      {
        auto der = cryptography::rsa::publickey::der::encode(owner);
        auto hash = cryptography::hash(
          elle::sprintf("NB/%s/%s", std::move(der), name),
          cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<blocks::Block>
      NB::clone(bool) const
      {
        return std::unique_ptr<blocks::Block>(new NB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      NB::_seal()
      {
        if (this->_keys)
          ELLE_ASSERT_EQ(this->_keys->K(), this->owner());
        else
          ELLE_ASSERT_EQ(this->doughnut()->keys().K(), this->owner());
        auto sign = this->_data_sign();
        auto const& key = _keys ? _keys->k() : this->doughnut()->keys().k();
        this->_signature = key.sign(sign);
      }

      elle::Buffer
      NB::_data_sign() const
      {
        elle::Buffer res;
        {
          elle::IOStream output(res.ostreambuf());
          elle::serialization::json::SerializerOut s(output);
          s.serialize("name", this->name());
          s.serialize("data", this->data());
        }
        return res;
      }

      // FIXME: factor with CHB
      blocks::ValidationResult
      NB::_validate() const
      {
        ELLE_TRACE("%s: check address", *this)
        {
          auto expected_address = NB::address(this->owner(), this->name());
          if (this->address() != expected_address)
          {
            auto reason = elle::sprintf("address %x invalid, expecting %x",
                                        this->address(), expected_address);
            ELLE_DEBUG("%s: %s", *this, reason);
            return blocks::ValidationResult::failure(reason);
          }
        }
        ELLE_TRACE("%s: check signature", *this)
        {
          auto signed_data = this->_data_sign();
          if (!this->_owner.verify(this->signature(), signed_data))
          {
            ELLE_DEBUG("%s: invalid signature", *this);
            return blocks::ValidationResult::failure("invalid signature");
          }
        }
        return blocks::ValidationResult::success();
      }

      /*--------------.
      | Serialization |
      `--------------*/

      NB::NB(elle::serialization::SerializerIn& input,
             elle::Version const& version)
        : Super(input, version)
        , _doughnut(nullptr)
        , _owner(input.deserialize<cryptography::rsa::PublicKey>("owner"))
        , _name(input.deserialize<std::string>("name"))
        , _signature(input.deserialize<elle::Buffer>("signature"))
      {
        input.serialize_context<Doughnut*>(this->_doughnut);
      }

      void
      NB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s, version);
        this->_serialize(s);
      }

      void
      NB::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("owner", this->_owner);
        s.serialize("name", this->_name);
        s.serialize("signature", this->_signature);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<NB> _register_nb_serialization("NB");
    }
  }
}
