#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <cryptography/KeyPair.hh>
#include <cryptography/PublicKey.hh>
#include <cryptography/SecretKey.hh>

#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.ACB");

struct ACLEntry
{
  infinit::cryptography::PublicKey key;
  bool read;
  bool write;
  elle::Buffer token;
};
DAS_MODEL(ACLEntry, (key, read, write, token));
DAS_MODEL_SERIALIZE(ACLEntry);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      ACL::ACL()
        : _contents()
        , _version(-1)
        , _signature()
      {}

      void
      ACL::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("contents", this->_contents);
        s.serialize("version", this->_version);
        s.serialize("signature", this->_signature);
      }

      /*-------------.
      | Construction |
      `-------------*/

      ACB::ACB(Doughnut* owner)
        : Super(owner)
        , _acl()
      {}

      /*-----------.
      | Validation |
      `-----------*/

      bool
      ACB::_validate(blocks::Block const& previous) const
      {
        if (!this->_validate())
          return false;
        auto previous_acb = dynamic_cast<ACB const*>(&previous);
        return previous_acb && this->version() > previous_acb->version();
      }

      bool
      ACB::_validate() const
      {
        if (!Super::_validate())
          return false;
        return true;
      }

      void
      ACB::_seal()
      {
        ELLE_TRACE_SCOPE("%s: update ACL", *this);
        // auto mine = this->doughnut()->keys().K() == this->owner_key();
        auto secret = cryptography::SecretKey::generate
          (cryptography::cipher::Algorithm::aes256, 256);
        ELLE_DUMP("%s: new block secret: %s", *this, secret);
        this->_owner_token =
          std::move(this->owner_key().encrypt(secret).buffer());
        std::vector<ACLEntry> entries;
        auto acl_address = this->_acl.contents();
        if (acl_address != Address::null)
        {
          auto acl = this->doughnut()->fetch(acl_address);
          ELLE_DUMP("%s: previous ACL: %s", *this, *acl);
          elle::IOStream input(acl->data().istreambuf());
          elle::serialization::json::SerializerIn s(input);
          s.serialize("entries", entries);
        }
        bool changed = false;
        int idx = 0;
        for (auto& e: entries)
        {
          if (e.write)
          {
            changed = true;
            e.token = std::move(e.key.encrypt(secret).buffer());
          }
          if (e.key == this->doughnut()->keys().K())
            this->_editor = idx;
          ++idx;
        }
        if (changed)
        {
          ELLE_TRACE_SCOPE("%s: store new ACL", *this);
          elle::Buffer new_acl;
          {
            elle::IOStream output(new_acl.ostreambuf());
            elle::serialization::json::SerializerOut s(output);
            s.serialize("entries", entries);
          }
          auto new_acl_block =
            this->doughnut()->make_block<blocks::ImmutableBlock>(new_acl);
          this->doughnut()->store(*new_acl_block);
          this->_acl._contents = new_acl_block->address();
        }
        Super::_seal();
        if (changed)
        {
          this->doughnut()->remove(acl_address);
        }
      }

      void
      ACB::_sign(elle::serialization::SerializerOut& s) const
      {
        Super::_sign(s);
        s.serialize("owner_token", this->_owner_token);
        s.serialize("acl_address", this->_acl._contents);
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACB::ACB(elle::serialization::Serializer& input)
        : Super(input)
      {
        this->_serialize(input);
      }

      void
      ACB::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        this->_serialize(s);
      }

      void
      ACB::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("editor", this->_editor);
        s.serialize("owner_token", this->_owner_token);
        s.serialize("acl", this->_acl);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
    }
  }
}
