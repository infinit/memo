#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/NB.hh>

#include <elle/log.hh>

#include <elle/cryptography/hash.hh>
#include <elle/cryptography/rsa/KeyPair.hh>

#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("memo.model.doughnut.NB");

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      NB::NB(Doughnut& doughnut,
             std::shared_ptr<elle::cryptography::rsa::PublicKey> owner,
             std::string name,
             elle::Buffer data,
             elle::Buffer signature)
        : Super(NB::address(*owner, name, doughnut.version()), std::move(data))
        , _doughnut(doughnut)
        , _owner(std::move(owner))
        , _name(std::move(name))
        , _signature(std::move(signature))
      {}

      NB::NB(Doughnut& doughnut,
             std::string name,
             elle::Buffer data,
             elle::Buffer signature)
        : NB(doughnut,
             doughnut.keys().public_key(),
             std::move(name),
             std::move(data),
             std::move(signature))
      {}

      NB::NB(NB const& other)
        : Super(other)
        , _doughnut(other._doughnut)
        , _owner(other._owner)
        , _name(other._name)
        , _signature(other._signature)
      {}

      Address
      NB::address(elle::cryptography::rsa::PublicKey const& owner,
                  std::string const& name, elle::Version const& version)
      {
        auto der = elle::cryptography::rsa::publickey::der::encode(owner);
        auto hash = elle::cryptography::hash(
          elle::sprintf("NB/%s/%s", std::move(der), name),
          elle::cryptography::Oneway::sha256);
        return Address(hash.contents(), flags::immutable_block,
                       version >= elle::Version(0, 5, 0));
      }

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<blocks::Block>
      NB::clone() const
      {
        return std::unique_ptr<blocks::Block>(new NB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      NB::_seal(boost::optional<int>)
      {
        if (!this->_signature.empty())
          return;
        ELLE_ASSERT_EQ(this->doughnut().keys().K(), *this->owner());
        auto sign = this->_data_sign();
        this->_signature = this->doughnut().keys().k().sign(sign);
      }

      elle::Buffer
      NB::_data_sign() const
      {
        bool pre04 = this->_doughnut.version() < elle::Version(0, 4, 0);
        elle::Buffer res;
        {
          elle::IOStream output(res.ostreambuf());
          elle::serialization::json::SerializerOut s(output, pre04);
          s.serialize("name", this->name());
          s.serialize("data", this->data());
        }
        return res;
      }

      // FIXME: factor with CHB
      blocks::ValidationResult
      NB::_validate(Model const& model, bool writing) const
      {
        Address expected_address;
        ELLE_DEBUG("%s: check address", *this)
        {
          expected_address = NB::address(*this->owner(), this->name(),
                                         this->_doughnut.version());
          if (!equal_unflagged(this->address(), expected_address))
          {
            auto reason = elle::sprintf("address %x invalid, expecting %x",
                                        this->address(), expected_address);
            ELLE_DEBUG("%s: %s", *this, reason);
            return blocks::ValidationResult::failure(reason);
          }
        }
        ELLE_DEBUG("%s: check signature", *this)
        {
          auto signed_data = this->_data_sign();
          if (!this->_owner->verify(this->signature(), signed_data))
          {
            ELLE_DEBUG("%s: invalid signature", *this);
            return blocks::ValidationResult::failure("invalid signature");
          }
        }
        /*
         if (this->_doughnut.version() >= elle::Version(0, 5, 0))
          elle::unconst(this)->_address = expected_address; // upgrade from unmasked if required
        */
        return blocks::ValidationResult::success();
      }

      blocks::ValidationResult
      NB::_validate(Model const& model, const Block& new_block) const
      {
        auto nb = dynamic_cast<const NB*>(&new_block);
        if (nb)
        {
          if (this->_name == nb->_name
            && this->_owner == nb->_owner
            && this->data() == nb->data())
          return blocks::ValidationResult::success();
        }
        return blocks::ValidationResult::failure("NB overwrite denied");
      }

      blocks::RemoveSignature
      NB::_sign_remove(Model& model) const
      {
        blocks::RemoveSignature res;
        res.block.reset(new NB(this->_doughnut,
                               this->_name,
                               elle::Buffer("MEMO_REMOVE", 14)));
        res.block->seal();
        return res;
      }

      blocks::ValidationResult
      NB::_validate_remove(Model& model,
                           blocks::RemoveSignature const& sig) const
      {
        if (!sig.block)
          return blocks::ValidationResult::failure("No block in signature");
        NB* other = dynamic_cast<NB*>(sig.block.get());
        if (!other)
          return blocks::ValidationResult::failure("not a NB");
        auto val = other->validate(model, true);
        if (!val)
          return val;
        if (other->address() != this->address())
          return blocks::ValidationResult::failure("Address mismatch");
        // redundant check, same address+validated implies same key
        if (*other->owner() != *this->owner())
          return blocks::ValidationResult::failure("Key mismatch (wow)");
        if (other->data() != elle::Buffer("MEMO_REMOVE", 14))
          return blocks::ValidationResult::failure("Invalid payload");
        return blocks::ValidationResult::success();
      }

      /*--------------.
      | Serialization |
      `--------------*/

      static
      Doughnut&
      dht(elle::serialization::SerializerIn& input)
      {
        Doughnut* res = nullptr;
        input.serialize_context<Doughnut*>(res);
        return *res;
      }

      NB::NB(elle::serialization::SerializerIn& input,
             elle::Version const& version)
        : Super(input, version)
        , _doughnut(dht(input))
        , _owner(std::make_shared(
                   input.deserialize<elle::cryptography::rsa::PublicKey>("owner")))
        , _name(input.deserialize<std::string>("name"))
        , _signature(input.deserialize<elle::Buffer>("signature"))
      {}

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
        s.serialize("owner", *this->_owner);
        s.serialize("name", this->_name);
        s.serialize("signature", this->_signature);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<NB> _register_nb_serialization("NB");
    }
  }
}
