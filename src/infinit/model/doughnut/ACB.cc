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
#include <infinit/model/doughnut/User.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.ACB");

struct ACLEntry
{
  infinit::cryptography::PublicKey key;
  bool read;
  bool write;
  elle::Buffer token;
};
DAS_MODEL(ACLEntry, (key, read, write, token), DasACLEntry);
DAS_MODEL_DEFAULT(ACLEntry, DasACLEntry);
DAS_MODEL_DEFINE(ACLEntry, (key, read, write), DasACLEntryPermissions);
DAS_MODEL_SERIALIZE(ACLEntry);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      ACB::ACB(Doughnut* owner)
        : Super(owner)
        , _editor(-1)
        , _owner_token()
        , _acl()
        , _acl_changed(true)
        , _data_version(-1)
        , _data_signature()
      {}

      /*--------.
      | Content |
      `--------*/

      elle::Buffer
      ACB::_decrypt_data(elle::Buffer const& data) const
      {
        auto& mine = this->doughnut()->keys().K();
        elle::Buffer const* encrypted_secret = nullptr;
        std::vector<ACLEntry> entries;
        if (mine == this->owner_key())
        {
          ELLE_DEBUG("%s: we are owner", *this);
          encrypted_secret = &this->_owner_token;
        }
        else if (this->_acl != Address::null)
        {
          // FIXME: factor searching the token
          auto acl = this->doughnut()->fetch(this->_acl);
          entries =
            elle::serialization::deserialize
              <std::vector<ACLEntry>, elle::serialization::Json>
            (acl->data(), "entries");
          auto it = std::find_if
            (entries.begin(), entries.end(),
             [&] (ACLEntry const& e) { return e.key == mine; });
          if (it != entries.end() && it->read)
          {
            ELLE_DEBUG("%s: we are an editor", *this);
            encrypted_secret = &it->token;
          }
        }
        if (!encrypted_secret)
        {
          ELLE_ABORT("no read permissions");
          // FIXME: better exceptions
          throw elle::Error("no read permissions");
        }
        auto secret =
          this->doughnut()->keys().k().decrypt<cryptography::SecretKey>
          (cryptography::Code(*encrypted_secret));
        ELLE_DUMP("%s: secret: %s", *this, secret);
        return
          std::move(secret.decrypt(cryptography::Code(this->_data)).buffer());
      }

      /*------------.
      | Permissions |
      `------------*/

      void
      ACB::set_permissions(cryptography::PublicKey const& key,
                           bool read,
                           bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permisions for %s: %s, %s",
                   *this, key, read, write);
        std::vector<ACLEntry> entries;
        if (this->_acl != Address::null)
        {
          ELLE_DEBUG_SCOPE("%s: fetch old ACL at %s", *this, this->_acl);
          auto acl = this->doughnut()->fetch(this->_acl);
          ELLE_DUMP("%s: ACL content: %s", acl->data());
          entries =
            elle::serialization::deserialize
            <std::vector<ACLEntry>, elle::serialization::Json>
            (acl->data(), "entries");
        }
        ELLE_DUMP("%s: ACL entries: %s", *this, entries);
        auto it = std::find_if
          (entries.begin(), entries.end(),
           [&] (ACLEntry const& e) { return e.key == key; });
        bool acl_changed = false;
        if (it == entries.end())
        {
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // FIXME: shitty crypto interface prevent me from just reencrypting
          // the buffer without deserializing the private key
          auto secret = this->doughnut()->keys().k().decrypt
            <cryptography::SecretKey>(cryptography::Code(this->_owner_token));
          auto token = key.encrypt(secret);
          entries.emplace_back
            (ACLEntry{key, read, write, std::move(token.buffer())});
          acl_changed = true;
        }
        else
        {
          ELLE_DEBUG_SCOPE("%s: edit ACL entry", *this);
          if (it->read != read)
          {
            it->read = read;
            acl_changed = true;
          }
          if (it->write != write)
          {
            it->write = write;
            acl_changed = true;
          }
        }
        if (acl_changed)
        {
          ELLE_DEBUG_SCOPE("%s: push new ACL block", *this);
          // FIXME: squash multiple ACL changes
          auto new_acl = this->doughnut()->make_block<blocks::ImmutableBlock>(
            elle::serialization::serialize
            <std::vector<ACLEntry>, elle::serialization::Json>
            (entries, "entries"));
          this->doughnut()->store(*new_acl);
          this->_acl = new_acl->address();
          ELLE_DUMP("%s: new ACL address: %s", *this, this->_acl);
          this->_acl_changed = true;
        }
      }

      void
      ACB::_set_permissions(model::User const& user_, bool read, bool write)
      {
        try
        {
          auto& user = dynamic_cast<User const&>(user_);
          this->set_permissions(user.key(), read, write);
        }
        catch (std::bad_cast const&)
        {
          ELLE_ABORT("doughnut was passed a non-doughnut user.");
        }
      }

      /*-----------.
      | Validation |
      `-----------*/

      bool
      ACB::_validate(blocks::Block const& previous) const
      {
        if (!Super::_validate(previous))
          return false;
        if (!this->_validate_version<ACB>
            (previous, &ACB::_data_version, this->_data_version))
          return false;
        return true;
      }

      bool
      ACB::_validate() const
      {
        ELLE_DEBUG("%s: check owner signature", *this)
          if (!Super::_validate())
            return false;
        ACLEntry* entry = nullptr;
        std::vector<ACLEntry> entries;
        if (this->_editor != -1)
        {
          ELLE_DEBUG_SCOPE("%s: check author has write permissions", *this);
          if (this->_acl == Address::null || this->_editor < 0)
            return false;
          auto acl = this->doughnut()->fetch(this->_acl);
          elle::IOStream input(acl->data().istreambuf());
          elle::serialization::json::SerializerIn s(input);
          s.serialize("entries", entries);
          if (this->_editor >= signed(entries.size()))
            return false;
          entry = &entries[this->_editor];
          if (!entry->write)
            return false;
        }
        ELLE_DEBUG("%s: check author signature", *this)
        {
          auto sign = this->_data_sign();
          auto& key = entry ? entry->key : this->owner_key();
          if (!this->_check_signature(key, this->_data_signature, sign, "data"))
            return false;
        }
        return true;
      }

      void
      ACB::_seal()
      {
        bool acl_changed = this->_acl_changed;
        bool data_changed = this->_data_changed;
        if (acl_changed)
        {
          ELLE_DEBUG_SCOPE("%s: ACL changed, seal", *this);
          Super::_seal_okb();
          this->_acl_changed = false;
        }
        else
          ELLE_DEBUG("%s: ACL didn't change", *this);
        if (data_changed)
        {
          ++this->_data_version; // FIXME: idempotence in case the write fails ?
          ELLE_TRACE_SCOPE("%s: data changed, seal", *this);
          bool owner = this->doughnut()->keys().K() == this->owner_key();
          if (owner)
            this->_editor = -1;
          auto secret = cryptography::SecretKey::generate
            (cryptography::cipher::Algorithm::aes256, 256);
          ELLE_DUMP("%s: new block secret: %s", *this, secret);
          this->_owner_token =
            std::move(this->owner_key().encrypt(secret).buffer());
          std::vector<ACLEntry> entries;
          auto acl_address = this->_acl;
          if (acl_address != Address::null)
          {
            auto acl = this->doughnut()->fetch(acl_address);
            ELLE_DUMP("%s: previous ACL: %s", *this, *acl);
            elle::IOStream input(acl->data().istreambuf());
            elle::serialization::json::SerializerIn s(input);
            s.serialize("entries", entries);
          }
          bool changed = false;
          bool found = false;
          int idx = 0;
          for (auto& e: entries)
          {
            if (e.read)
            {
              changed = true;
              e.token = std::move(e.key.encrypt(secret).buffer());
            }
            if (e.key == this->doughnut()->keys().K())
            {
              found = true;
              this->_editor = idx;
            }
            ++idx;
          }
          if (!owner && !found)
            throw elle::Error("not owner and no write permissions");
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
            this->_acl = new_acl_block->address();
          }
          auto encrypted =
            secret.encrypt(cryptography::Plain(this->data_plain()));
          this->MutableBlock::data(std::move(encrypted.buffer()));
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
        // Even if only the ACL was changed, we need to re-sign because the ACL
        // address is part of the signature.
        if (acl_changed || data_changed)
        {
          auto sign = this->_data_sign();
          auto const& key = this->doughnut()->keys().k();
          this->_data_signature = key.sign(cryptography::Plain(sign));
          ELLE_DUMP("%s: sign %s with %s: %f",
                    *this, sign, key, this->_data_signature);
        }
      }

      elle::Buffer
      ACB::_data_sign() const
      {
        elle::Buffer res;
        {
          elle::IOStream output(res.ostreambuf());
          elle::serialization::json::SerializerOut s(output);
          s.serialize("block_key", this->key());
          s.serialize("version", this->_data_version);
          s.serialize("data", this->Block::data());
          s.serialize("owner_token", this->_owner_token);
          s.serialize("acl", this->_acl);
        }
        return res;
      }

      void
      ACB::_sign(elle::serialization::SerializerOut& s) const
      {
        std::vector<ACLEntry> entries;
        if (this->_acl != Address::null)
        {
          auto acl = this->doughnut()->fetch(this->_acl);
          entries = elle::serialization::deserialize
            <std::vector<ACLEntry>, elle::serialization::Json>
            (acl->data(), "entries");
        }
        s.elle::serialization::Serializer::serialize(
          "acls", entries,
          elle::serialization::as<das::Serializer<DasACLEntryPermissions>>());
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACB::ACB(elle::serialization::Serializer& input)
        : Super(input)
        , _editor(-2)
        , _owner_token()
        , _acl()
        , _acl_changed(false)
        , _data_version(-1)
        , _data_signature()
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
        s.serialize("data_version", this->_data_version);
        s.serialize("data_signature", this->_data_signature);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
    }
  }
}
