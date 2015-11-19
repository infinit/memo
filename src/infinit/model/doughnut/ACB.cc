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
DAS_MODEL_SERIALIZE(infinit::model::doughnut::ACB::ACLEntry);

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
        DasACLEntry::Update content(s);
        return ACLEntry(std::move(content.key.get()),
                        content.read.get(),
                        content.write.get(),
                        std::move(content.token.get()));
      }

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

      ACB::ACB(ACB const& other)
        : Super(other)
        , _editor(other._editor)
        , _owner_token(other._owner_token)
        , _acl(other._acl)
        , _acl_changed(other._acl_changed)
        , _acl_entries(other._acl_entries)
        , _data_version(other._data_version)
        , _data_signature(other._data_signature)
      {

      }

      /*-------.
      | Clone  |
      `-------*/
      std::unique_ptr<blocks::Block>
      ACB::clone() const
      {
        return std::unique_ptr<blocks::Block>(new ACB(*this));
      }

      /*--------.
      | Content |
      `--------*/

      class ACBCache: public blocks::MutableBlock::Cache
      {
      public:
        ACBCache() {}
        ACBCache(elle::Buffer m, elle::Buffer c)
        : md5(std::move(m))
        , cleartext(std::move(c))
        {}
        elle::Buffer md5;
        elle::Buffer cleartext;
      };

      std::unique_ptr<blocks::MutableBlock::Cache>
      ACB::cache_update(std::unique_ptr<blocks::MutableBlock::Cache> prev)
      {
        static bool disable = getenv("INFINIT_ACB_NO_CACHE");
        if (disable)
          return std::move(prev);
        elle::Buffer md5;
        {
          static elle::Bench bench("bench.md5", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          md5 = infinit::cryptography::hash(Block::data(),
                                            infinit::cryptography::Oneway::md5);
        }
        if (prev)
        {
          ACBCache& cache = dynamic_cast<ACBCache&>(*prev);
          static elle::Bench bench("bench.cache", 1000_sec);

          if (md5 == cache.md5)
          {
            bench.add(1);
            this->_data_plain = elle::Buffer(cache.cleartext);
            this->_data_decrypted = true;
            return std::move(prev);
          }
          else
            bench.add(0);
        }
        elle::Buffer decrypted = this->data();
        return elle::make_unique<ACBCache>(std::move(md5), std::move(decrypted));
      }

      int
      ACB::version() const
      {
        return this->_data_version;
      }

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
          auto acl = this->_fetch_acl();
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
          // FIXME: better exceptions
          throw ValidationFailed("no read permissions");
        }
        auto secret_buffer =
          this->doughnut()->keys().k().open(*encrypted_secret);
        auto secret =
          elle::serialization::deserialize
          <cryptography::SecretKey, elle::serialization::Json>
          (secret_buffer);
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
        auto& acl_entries = this->acl_entries();
        ELLE_DUMP("%s: ACL entries: %s", *this, acl_entries);
        auto it = std::find_if
          (acl_entries.begin(), acl_entries.end(),
           [&] (ACLEntry const& e) { return e.key == key; });
        if (it == acl_entries.end())
        {
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // If the owner token is empty, this block was never pushed and
          // sealing will generate a new secret and update the token.
          elle::Buffer token;
          if (this->_owner_token.size())
          {
            auto secret = this->doughnut()->keys().k().open(this->_owner_token);
            token = key.seal(secret);
          }
          acl_entries.emplace_back(ACLEntry(key, read, write, token));
          this->_acl_changed = true;
        }
        else
        {
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
        // also add owner in case it's not the same
        other->set_permissions(this->owner_key(), true, true);
        if (this->_acl == Address::null)
          return; // nothing to do
        auto acl = this->_fetch_acl();
        std::vector<ACLEntry> entries;
        entries =
          elle::serialization::deserialize
          <std::vector<ACLEntry>, elle::serialization::Json>
          (acl->data(), "entries");
        // FIXME: better implementation
        for (auto const& e: entries)
        {
          other->set_permissions(e.key, e.read, e.write);
        }
      }



      std::vector<ACB::Entry>
      ACB::_list_permissions(bool ommit_names)
      {
        std::vector<ACB::Entry> res;
        try
        {
          std::unique_ptr<model::User> user;
          if (ommit_names)
            user.reset(new doughnut::User(this->owner_key(), ""));
          else
            user = this->doughnut()->make_user(
              elle::serialization::serialize
                <cryptography::rsa::PublicKey, elle::serialization::Json>(
                  this->owner_key()));
          res.emplace_back(std::move(user), true, true);
        }
        catch (reactor::Terminate const& e)
        {
          throw;
        }
        catch(std::exception const& e)
        {
          ELLE_TRACE("Exception making owner: %s", e);
        }
        if (this->_acl == Address::null)
          return std::move(res);
        auto acl = this->_fetch_acl();
        std::vector<ACLEntry> entries;
        entries =
          elle::serialization::deserialize
          <std::vector<ACLEntry>, elle::serialization::Json>
          (acl->data(), "entries");
        for (auto const& ent: entries)
        {
          try
          {
            std::unique_ptr<model::User> user;
            if (ommit_names)
              user.reset(new doughnut::User(ent.key, ""));
            else
              user = this->doughnut()->make_user(
                elle::serialization::serialize
                <cryptography::rsa::PublicKey, elle::serialization::Json>(
                  ent.key));
            res.emplace_back(std::move(user), ent.read, ent.write);
          }
          catch(reactor::Terminate const& e)
          {
            throw;
          }
          catch(std::exception const& e)
          {
            ELLE_TRACE("Exception making user: %s", e);
            res.emplace_back(elle::make_unique<model::User>(), ent.read, ent.write);
          }
        }
        return res;
      }

      std::vector<ACB::ACLEntry>&
      ACB::acl_entries()
      {
        if (!this->_acl_entries)
          if (this->_acl != Address::null)
          {
            ELLE_DEBUG_SCOPE("%s: fetch old ACL at %s", *this, this->_acl);
            auto acl = this->_fetch_acl();
            ELLE_DUMP("%s: ACL content: %s", *this, acl->data());
            this->_acl_entries =
              elle::serialization::deserialize
              <std::vector<ACLEntry>, elle::serialization::Json>
              (acl->data(), "entries");
          }
          else
            this->_acl_entries.emplace();
        return *this->_acl_entries;
      }

      std::vector<ACB::ACLEntry> const&
      ACB::acl_entries() const
      {
        return const_cast<Self*>(this)->acl_entries();
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
        std::vector<ACLEntry> entries;
        if (this->_editor != -1)
        {
          ELLE_DEBUG_SCOPE("%s: check author has write permissions", *this);
          if (this->_acl == Address::null || this->_editor < 0)
          {
            ELLE_DEBUG("%s: no ACL or no editor", *this);
            return blocks::ValidationResult::failure("no ACL or no editor");
          }
          auto acl = this->_fetch_acl();
          elle::IOStream input(acl->data().istreambuf());
          elle::serialization::json::SerializerIn s(input);
          s.serialize("entries", entries);
          if (this->_editor >= signed(entries.size()))
          {
            ELLE_DEBUG("%s: editor index out of bounds", *this);
            return blocks::ValidationResult::failure
              ("editor index out of bounds");
          }
          entry = &entries[this->_editor];
          if (!entry->write)
          {
            ELLE_DEBUG("%s: no write permissions", *this);
            return blocks::ValidationResult::failure("no write permissions");
          }
        }
        ELLE_DEBUG("%s: check author signature", *this)
        {
          auto sign = this->_data_sign();
          auto& key = entry ? entry->key : this->owner_key();
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
      ACB::_seal()
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
          if (this->_acl_entries)
          {
            auto const& entries = *this->_acl_entries;
            ELLE_DEBUG_SCOPE("%s: push new ACL block", *this);
            {
              auto new_acl = this->doughnut()->make_block<blocks::ImmutableBlock>(
                elle::serialization::serialize
                <std::vector<ACLEntry>, elle::serialization::Json>
                (entries, "entries"));
              auto new_acl_address = new_acl->address();
              this->doughnut()->store(std::move(new_acl), STORE_INSERT);
              this->_prev_acl = this->_acl;
              this->_acl = new_acl_address;
              this->_acl_changed = true;
              ELLE_DUMP("%s: new ACL address: %s", *this, this->_acl);
            }
          }
          this->_acl_changed = false;
          Super::_seal_okb();
          if (!data_changed)
            // FIXME: idempotence in case the write fails ?
            ++this->_data_version;
        }
        else
          ELLE_DEBUG("%s: ACL didn't change", *this);
        if (data_changed)
        {
          static elle::Bench bench("bench.acb.seal.datachange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ++this->_data_version; // FIXME: idempotence in case the write fails ?
          ELLE_TRACE_SCOPE("%s: data changed, seal", *this);
          bool owner = this->doughnut()->keys().K() == this->owner_key();
          if (owner)
            this->_editor = -1;
          auto secret = cryptography::secretkey::generate(256);
          ELLE_DUMP("%s: new block secret: %s", *this, secret);
          auto secret_buffer =
            elle::serialization::serialize
            <cryptography::SecretKey, elle::serialization::Json>(secret);
          this->_owner_token = this->owner_key().seal(secret_buffer);
          std::vector<ACLEntry> entries;
          auto acl_address = this->_acl;
          if (acl_address != Address::null)
          {
            auto acl = this->_fetch_acl();
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
              e.token = e.key.seal(secret_buffer);
            }
            if (e.key == this->doughnut()->keys().K())
            {
              found = true;
              this->_editor = idx;
            }
            ++idx;
          }
          if (!owner && !found)
            throw ValidationFailed("not owner and no write permissions");
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
            this->_prev_acl = this->_acl;
            this->_acl = new_acl_block->address();
            this->doughnut()->store(std::move(new_acl_block), STORE_INSERT);
          }
          this->MutableBlock::data(secret.encipher(this->data_plain()));
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
        // Even if only the ACL was changed, we need to re-sign because the ACL
        // address is part of the signature.
        if (acl_changed || data_changed)
        {
          static elle::Bench bench("bench.acb.seal.signing", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          auto to_sign = elle::utility::move_on_copy(this->_data_sign());
          this->_data_signature =
            [this, to_sign]
            {
              return this->doughnut()->keys().k().sign(*to_sign);
            };
        }
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
          elle::serialization::binary::SerializerOut s(output);
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
          ELLE_ASSERT(this->doughnut());
          try
          {
            auto acl = this->_fetch_acl();
            entries = elle::serialization::deserialize
              <std::vector<ACLEntry>, elle::serialization::Json>
              (acl->data(), "entries");
          }
          catch (elle::Error const& e)
          {
            elle::throw_with_nested(
              elle::Error(
                elle::sprintf("unable to fetch ACL block %s", this->_acl)));
          }
        }
        s.serialize(
          "acls", entries,
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
        if (this->_prev_acl != Address::null)
        {
          this->doughnut()->remove(this->_prev_acl);
          this->_prev_acl = Address();
        }
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
        if (this->_acl != other_acb->_acl)
          return false;
        if (this->_data_version != other_acb->_data_version)
          return false;
        if (this->_data_signature.value() != other_acb->_data_signature.value())
          return false;
        return this->Super::operator ==(rhs);
      }

      std::unique_ptr<blocks::Block>
      ACB::_fetch_acl() const
      {
        try
        {
          return this->doughnut()->fetch(this->_acl);
        }
        catch (MissingBlock const& mb)
        { // assuming the block got updated
          auto new_self = this->doughnut()->fetch(this->address());
          auto acb = elle::cast<ACB>::runtime(new_self);
          if (acb->_acl == _acl)
            throw;
          const_cast<ACB*>(this)->_acl = acb->_acl;
          return this->doughnut()->fetch(this->_acl);
        }
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACB::ACB(elle::serialization::SerializerIn& input)
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
        bool need_signature = !s.context().has<ACBDontWaitForSignature>();
        if (need_signature)
          s.serialize("data_signature", this->_data_signature.value());
        else
        {
          elle::Buffer signature;
          s.serialize("data_signature", signature);
          if (s.in())
          {
            auto sign = elle::utility::move_on_copy(this->_data_sign());
            this->_data_signature =
              [sign, this]
              {
                return this->doughnut()->keys().k().sign(*sign);
              };
          }
        }
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
    }
  }
}
