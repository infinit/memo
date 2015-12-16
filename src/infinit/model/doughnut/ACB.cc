#include <infinit/model/doughnut/ACB.hh>

#include <boost/iterator/zip_iterator.hpp>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh>
#include <elle/utility/Move.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/SecretKey.hh>
#include <cryptography/hash.hh>

#include <reactor/exception.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/serialization.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.ACB");

DAS_MODEL_FIELDS(infinit::model::doughnut::ACB::ACLEntry,
                 (key, read, write, token));

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      DAS_MODEL_DEFINE(ACB::ACLEntry, (key, read, write, token),
                       DasACLEntry);
      DAS_MODEL_DEFINE(ACB::ACLEntry, (key, read, write),
                       DasACLEntryPermissions);
    }
  }
}

DAS_MODEL_DEFAULT(infinit::model::doughnut::ACB::ACLEntry,
                  infinit::model::doughnut::DasACLEntry);
// FAILS in binary mode
// DAS_MODEL_SERIALIZE(infinit::model::doughnut::ACB::ACLEntry);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*---------.
      | ACLEntry |
      `---------*/

      ACB::ACLEntry::ACLEntry(infinit::cryptography::rsa::PublicKey key_,
                              bool read_,
                              bool write_,
                              elle::Buffer token_)
        : key(std::move(key_))
        , read(read_)
        , write(write_)
        , token(std::move(token_))
      {}

      ACB::ACLEntry::ACLEntry(ACLEntry const& other)
        : key{other.key}
        , read{other.read}
        , write{other.write}
        , token{other.token}
      {}

      ACB::ACLEntry::ACLEntry(elle::serialization::SerializerIn& s)
        : ACLEntry(deserialize(s))
      {}

      ACB::ACLEntry
      ACB::ACLEntry::deserialize(elle::serialization::SerializerIn& s)
      {

        auto key = s.deserialize<cryptography::rsa::PublicKey>("key");
        auto read = s.deserialize<bool>("read");
        auto write = s.deserialize<bool>("write");
        auto token = s.deserialize<elle::Buffer>("token");
        return ACLEntry(std::move(key), read, write, std::move(token));

        /*
        DasACLEntry::Update content(s);
        return ACLEntry(std::move(content.key.get()),
                        content.read.get(),
                        content.write.get(),
                        std::move(content.token.get()));*/

      }


      void
      ACB::ACLEntry::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("key", key);
        s.serialize("read", read);
        s.serialize("write", write);
        s.serialize("token", token);
      }

      bool
      ACB::ACLEntry::operator == (ACB::ACLEntry const& b) const
      {
        return key == b.key && read == b.read && write && b.write
          && token == b.token;
      }

      /*-------------.
      | Construction |
      `-------------*/

      ACB::ACB(std::shared_ptr<cryptography::rsa::KeyPair> keys,
               elle::Buffer data,
               boost::optional<elle::Buffer> salt)
        : Super(std::move(keys), std::move(data), std::move(salt))
        , _editor(-1)
        , _owner_token()
        , _acl_changed(true)
        , _data_version(-1)
        , _data_signature()
      {}

      ACB::ACB(ACB const& other, bool sealed_copy)
        : Super(other, sealed_copy)
        , _editor(other._editor)
        , _owner_token(other._owner_token)
        , _acl_changed(other._acl_changed)
        , _acl_entries(other._acl_entries)
        , _data_version(other._data_version)
      {
        if (sealed_copy)
        {
          _data_signature = other._data_signature.value();
        }
        else
        {
          this->_data_signature = elle::Buffer();
        }
      }

      /*-------.
      | Clone  |
      `-------*/
      std::unique_ptr<blocks::Block>
      ACB::clone(bool sealed_copy) const
      {
        return std::unique_ptr<blocks::Block>(new ACB(*this, sealed_copy));
      }

      /*--------.
      | Content |
      `--------*/

      int
      ACB::version() const
      {
        return this->_data_version;
      }

      elle::Buffer
      ACB::_decrypt_data(elle::Buffer const& data) const
      {
        auto& mine = this->keys()->K();
        elle::Buffer const* encrypted_secret = nullptr;
        std::vector<ACLEntry> entries;
        if (mine == *this->owner_key())
        {
          ELLE_DEBUG("%s: we are owner", *this);
          encrypted_secret = &this->_owner_token;
        }
        else if (!this->_acl_entries.empty())
        {
          // FIXME: factor searching the token
          auto it = std::find_if
            (this->_acl_entries.begin(), this->_acl_entries.end(),
             [&] (ACLEntry const& e) { return e.key == mine; });
          if (it != this->_acl_entries.end() && it->read)
          {
            ELLE_DEBUG("%s: we are an editor", *this);
            encrypted_secret = &it->token;
          }
        }
        if (!encrypted_secret)
        {
          // FIXME: better exceptions
          throw ValidationFailed("no read permissions");
        }
        auto secret_buffer =
          this->keys()->k().open(*encrypted_secret);
        auto secret = elle::serialization::json::deserialize
          <cryptography::SecretKey>(secret_buffer);
        ELLE_DUMP("%s: secret: %s", *this, secret);
        return secret.decipher(this->_data);
      }

      /*------------.
      | Permissions |
      `------------*/

      void
      ACB::set_permissions(cryptography::rsa::PublicKey const& key,
                           bool read,
                           bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permisions for %s: %s, %s",
                         *this, key, read, write);
        if (key == *this->owner_key())
          throw elle::Error("Cannot set permissions for owner");
        auto& acl_entries = this->_acl_entries;
        ELLE_DUMP("%s: ACL entries: %s", *this, acl_entries);
        auto it = std::find_if
          (acl_entries.begin(), acl_entries.end(),
           [&] (ACLEntry const& e) { return e.key == key; });
        if (it == acl_entries.end())
        {
          if (!read && !write)
          {
            ELLE_DUMP("%s: new user with no read or write permissions, "
                      "do nothing", *this);
            return;
          }
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // If the owner token is empty, this block was never pushed and
          // sealing will generate a new secret and update the token.
          // FIXME: the block will always be sealed anyway, why encrypt a token
          // now ?
          elle::Buffer token;
          if (this->_owner_token.size())
          {
            auto secret = this->keys()->k().open(this->_owner_token);
            token = key.seal(secret);
          }
          acl_entries.emplace_back(ACLEntry(key, read, write, token));
          this->_acl_changed = true;
        }
        else
        {
          if (!read && !write)
          {
            ELLE_DEBUG_SCOPE("%s: user (%s) no longer has read or write "
                             "permissions, remove ACL entry", *this, key);
            acl_entries.erase(it);
            this->_acl_changed = true;
            return;
          }
          ELLE_DEBUG_SCOPE("%s: edit ACL entry", *this);
          if (it->read != read)
          {
            it->read = read;
            this->_acl_changed = true;
          }
          if (it->write != write)
          {
            it->write = write;
            this->_acl_changed = true;
          }
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

      void
      ACB::_copy_permissions(ACLBlock& to)
      {
        ACB* other = dynamic_cast<ACB*>(&to);
        if (!other)
          throw elle::Error("Other block is not an ACB");
        // FIXME: better implementation
        for (auto const& e: this->_acl_entries)
        {
          if (e.key != *other->owner_key())
            other->set_permissions(e.key, e.read, e.write);
        }
        if (*other->owner_key() != *this->owner_key())
          other->set_permissions(*this->owner_key(), true, true);
      }

      std::vector<ACB::Entry>
      ACB::_list_permissions(boost::optional<Model const&> model)
      {
        auto make_user =
          [&] (cryptography::rsa::PublicKey const& k)
          -> std::unique_ptr<infinit::model::User>
          {
            try
            {
              if (model)
                return
                  model->make_user(elle::serialization::json::serialize(k));
              else
                return elle::make_unique<doughnut::User>(k, "");
            }
            catch(elle::Error const& e)
            {
              ELLE_WARN("exception making user: %s", e);
            }
          };
        std::vector<ACB::Entry> res;
        res.emplace_back(make_user(*this->owner_key()), true, true);
        for (auto const& ent: this->_acl_entries)
          res.emplace_back(make_user(ent.key), ent.read, ent.write);
        return res;
      }

      /*-----------.
      | Validation |
      `-----------*/

      blocks::ValidationResult
      ACB::_validate() const
      {
        if (_is_local)
          return blocks::ValidationResult::success();
        ELLE_DEBUG("%s: validate owner part", *this)
          if (auto res = Super::_validate()); else
            return res;
        ELLE_DEBUG_SCOPE("%s: validate author part", *this);
        ACLEntry* entry = nullptr;
        if (this->_editor != -1)
        {
          ELLE_DEBUG_SCOPE("%s: check author has write permissions", *this);
          if (this->_editor < 0)
          {
            ELLE_DEBUG("%s: no ACL or no editor", *this);
            return blocks::ValidationResult::failure("no ACL or no editor");
          }
          if (this->_editor >= signed(this->_acl_entries.size()))
          {
            ELLE_DEBUG("%s: editor index out of bounds", *this);
            return blocks::ValidationResult::failure
              ("editor index out of bounds");
          }
          entry = elle::unconst(&this->_acl_entries[this->_editor]);
          if (!entry->write)
          {
            ELLE_DEBUG("%s: no write permissions", *this);
            return blocks::ValidationResult::failure("no write permissions");
          }
        }
        ELLE_DEBUG("%s: check author signature", *this)
        {
          auto sign = this->_data_sign();
          auto& key = entry ? entry->key : *this->owner_key();
          if (!this->_check_signature(key, this->data_signature(), sign, "data"))
          {
            ELLE_DEBUG("%s: author signature invalid", *this);
            return blocks::ValidationResult::failure
              ("author signature invalid");
          }
        }
        return blocks::ValidationResult::success();
      }

      void
      ACB::seal(cryptography::SecretKey const& key)
      {
        this->_seal(key);
      }

      void
      ACB::_seal()
      {
        this->_seal({});
      }

      void
      ACB::_seal(boost::optional<cryptography::SecretKey const&> key)
      {
        static elle::Bench bench("bench.acb.seal", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        bool acl_changed = this->_acl_changed;
        bool data_changed = this->_data_changed;
        if (acl_changed)
        {
          static elle::Bench bench("bench.acb.seal.aclchange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ELLE_DEBUG_SCOPE("%s: ACL changed, seal", *this);
          this->_acl_changed = false;
          bool owner = this->keys()->K() == *this->owner_key();
          if (owner)
            this->_editor = -1;
          Super::_seal_okb();
          if (!data_changed)
            // FIXME: idempotence in case the write fails ?
            ++this->_data_version;
        }
        else if (!this->_signature.running() && this->_signature.value().empty())
        {
          ELLE_DEBUG("%s: signature missing, recalculating...", *this);
          this->_seal_okb(false);
        }
        else
          ELLE_DEBUG("%s: ACL didn't change", *this);
        if (data_changed)
        {
          static elle::Bench bench("bench.acb.seal.datachange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ++this->_data_version; // FIXME: idempotence in case the write fails ?
          ELLE_TRACE_SCOPE("%s: data changed, seal version %s",
                           *this, this->_data_version);
          bool owner = this->keys()->K() == *this->owner_key();
          if (owner)
            this->_editor = -1;
          boost::optional<cryptography::SecretKey> secret;
          elle::Buffer secret_buffer;
          if (!key)
          {
            secret = cryptography::secretkey::generate(256);
            key = secret;
          }
          ELLE_DUMP("%s: new block secret: %s", *this, key.get());
          secret_buffer = elle::serialization::json::serialize(key.get());
          this->_owner_token = this->owner_key()->seal(secret_buffer);
          bool found = false;
          int idx = 0;
          for (auto& e: this->_acl_entries)
          {
            if (e.read)
            {
              e.token = e.key.seal(secret_buffer);
            }
            if (e.key == this->keys()->K())
            {
              found = true;
              this->_editor = idx;
            }
            ++idx;
          }
          if (!owner && !found)
            throw ValidationFailed("not owner and no write permissions");
          this->MutableBlock::data(key->encipher(this->data_plain()));
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
        // Even if only the ACL was changed, we need to re-sign because the ACL
        // address is part of the signature.
        if (acl_changed || data_changed ||
          (!this->_data_signature.running() && this->_data_signature.value().empty()))
        {
          auto keys = this->keys();
          auto to_sign = elle::utility::move_on_copy(this->_data_sign());
          this->_data_signature =
            [keys, to_sign]
            {
              static elle::Bench bench("bench.acb.seal.signing", 10000_sec);
              elle::Bench::BenchScope scope(bench);
              return keys->k().sign(*to_sign);
            };
        }
        Super::_seal();
      }

      ACB::~ACB()
      {}

      elle::Buffer const&
      ACB::data_signature() const
      {
        return this->_data_signature.value();
      }

      elle::Buffer
      ACB::_data_sign() const
      {
        elle::Buffer res;
        {
          elle::IOStream output(res.ostreambuf());
          elle::serialization::binary::SerializerOut s(output, false);
          s.serialize("salt", this->salt());
          s.serialize("key", *this->owner_key());
          s.serialize("version", this->_data_version);
          s.serialize("data", this->Block::data());
          s.serialize("owner_token", this->_owner_token);
          s.serialize("acl", this->_acl_entries);
        }
        return res;
      }

      void
      ACB::_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize(
          "acls", elle::unconst(this)->_acl_entries,
          elle::serialization::as<das::Serializer<DasACLEntryPermissions>>());
      }

      template <typename... T>
      auto zip(const T&... containers)
        -> boost::iterator_range<boost::zip_iterator<
             decltype(boost::make_tuple(std::begin(containers)...))>>
      {
        auto zip_begin = boost::make_zip_iterator(
          boost::make_tuple(std::begin(containers)...));
        auto zip_end = boost::make_zip_iterator(
          boost::make_tuple(std::end(containers)...));
        return boost::make_iterator_range(zip_begin, zip_end);
      }

      /*--------.
      | Content |
      `--------*/

      /*--------.
      | Content |
      `--------*/

      void
      ACB::_stored()
      {
      }

      bool
      ACB::operator ==(blocks::Block const& rhs) const
      {
        auto other_acb = dynamic_cast<ACB const*>(&rhs);
        if (!other_acb)
          return false;
        if (this->_editor != other_acb->_editor)
          return false;
        if (this->_owner_token != other_acb->_owner_token)
          return false;
        if (this->_acl_entries != other_acb->_acl_entries)
          return false;
        if (this->_data_version != other_acb->_data_version)
          return false;
        if (this->_data_signature.value() != other_acb->_data_signature.value())
          return false;
        return this->Super::operator ==(rhs);
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACB::ACB(elle::serialization::SerializerIn& input)
        : Super(input)
        , _editor(-2)
        , _owner_token()
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
        s.serialize("acl", this->_acl_entries);
        s.serialize("data_version", this->_data_version);
        bool need_signature = !s.context().has<ACBDontWaitForSignature>();
        if (need_signature)
          s.serialize("data_signature", this->_data_signature.value());
        else
        {
          elle::Buffer signature;
          s.serialize("data_signature", signature);
          if (s.in())
          {
            auto keys = this->keys();
            auto sign = elle::utility::move_on_copy(this->_data_sign());
            this->_data_signature =
              [keys, sign] { return keys->k().sign(*sign); };
          }
        }
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
    }
  }
}
