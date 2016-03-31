#include <elle/bench.hh>
#include <elle/log.hh>

#include <cryptography/hash.hh>

#include <reactor/duration.hh>
#include <reactor/scheduler.hh>

#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.CHB")

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      CHB::CHB(Doughnut* d, elle::Buffer data, Address owner)
        : CHB(d, std::move(data), this->_make_salt(), owner)
      {}

      CHB::CHB(Doughnut* d, elle::Buffer data, elle::Buffer salt, Address owner)
        : Super(CHB::_hash_address(data, owner, salt, d->version()), data)
        , _salt(std::move(salt))
        , _owner(owner)
      {
        if (d->version() < elle::Version(0, 4, 0))
          this->_owner = Address::null;
      }

      CHB::CHB(CHB const& other)
        : Super(other)
        , _salt(other._salt)
        , _owner(other._owner)
      {}

      /*---------.
      | Clonable |
      `---------*/

      std::unique_ptr<blocks::Block>
      CHB::clone() const
      {
        return std::unique_ptr<blocks::Block>(new CHB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      CHB::_seal(boost::optional<int>)
      {}

      blocks::ValidationResult
      CHB::_validate(Model const& model) const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        auto expected_address =
          CHB::_hash_address(this->data(), this->_owner,
                             this->_salt, model.version());
        if (!equal_unflagged(this->address(), expected_address))
        {
          auto reason =
            elle::sprintf("address %x invalid, expecting %x",
                          this->address(), expected_address);
          ELLE_DUMP("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        /*
        if (this->_doughnut->version() >= elle::Version(0, 5, 0))
          elle::unconst(this)->_address = expected_address; // upgrade from unmasked if required
        */
        return blocks::ValidationResult::success();
      }

      /*--------------.
      | Serialization |
      `--------------*/

      CHB::CHB(elle::serialization::Serializer& input,
               elle::Version const& version)
        : Super(input, version)
      {
        input.serialize("salt", _salt);
        if (version >= elle::Version(0, 4, 0))
          input.serialize("owner", _owner);
      }

      void
      CHB::serialize(elle::serialization::Serializer& s,
                     elle::Version const& version)
      {
        Super::serialize(s, version);
        s.serialize("salt", _salt);
        if (version >= elle::Version(0, 4, 0))
          s.serialize("owner", _owner);
      }

      /*--------.
      | Details |
      `--------*/

      elle::Buffer
      CHB::_make_salt()
      {
        return elle::Buffer(Address::random(0).value(), // FIXME
                            sizeof(Address::Value));
      }

      blocks::RemoveSignature
      CHB::_sign_remove(Model& model) const
      {
        auto& dht = dynamic_cast<Doughnut&>(model);
        ELLE_TRACE("%s: sign_remove, owner=%x", *this, this->_owner);
        if (this->_owner == Address::null)
          return blocks::RemoveSignature();
        blocks::RemoveSignature res;
        // we need to figure out which key to use, the one giving us access to the owner
        elle::Buffer to_sign(this->address().value(), sizeof(Address::Value));
        elle::Buffer signature;
        auto& keys = dht.keys();
        auto block = dht.fetch(this->_owner);
        // default behavior if signature not set below is to use doughnut key
        if (!block)
        {
          ELLE_WARN("CHB owner %x not found, cannot sign remove request",
            this->_owner);
        }
        else
        {
          auto* acb = dynamic_cast<ACB*>(block.get());
          if (!acb)
          {
            ELLE_WARN("CHB owner %x is not an ACB, cannot signe remove request",
              this->_owner);
          }
          else
          {
            if (*acb->owner_key() != keys.K())
            {
              auto& entries = acb->acl_entries();
              auto it = std::find_if(entries.begin(), entries.end(),
                [&](ACLEntry const& e) { return e.key == keys.K();});
              if (it == entries.end() || !it->write)
              {
                // group check
                auto& entries = acb->acl_group_entries();
                for (auto const& e: entries)
                {
                  try
                  {
                    Group g(dht, e.key);
                    auto kp = g.current_key();
                    ELLE_TRACE("Using group key");
                    signature = kp.k().sign(to_sign);
                    res.signature_key.emplace(kp.K());
                    res.group_key.emplace(e.key);
                    res.group_index = g.version() - 1;
                    break;
                  }
                  catch (elle::Error const&)
                  { // group access denied
                  }
                }
              }
            }
          }
        }
        if (signature.empty())
        {
          signature = keys.k().sign(to_sign);
          res.signature_key.emplace(keys.K());
        }
        res.signature.emplace(signature);
        return res;
      }

      blocks::ValidationResult
      CHB::_validate_remove(Model& model,
                            blocks::RemoveSignature const& sig) const
      {
        auto& dht = dynamic_cast<Doughnut&>(model);
        ELLE_TRACE("%s: validate_remove", *this);
        if (this->_owner == Address::null)
          return blocks::ValidationResult::success();
        if (!sig.signature_key || !sig.signature)
          return blocks::ValidationResult::failure("Missing field in signature");
        auto& key = *sig.signature_key;
        bool ok = key.verify(*sig.signature,
          elle::ConstWeakBuffer(this->address().value(), sizeof(Address::Value)));
        if (!ok)
          return blocks::ValidationResult::failure("Invalid signature");
        // now verify that this key has access to owner
        auto block = model.fetch(this->_owner);
        if (!block)
        {
          ELLE_WARN("CHB owner %x not found, cannot validate remove request",
            this->_owner);
          return blocks::ValidationResult::success();
        }
        auto* acb = dynamic_cast<ACB*>(block.get());
        if (!acb)
        {
          ELLE_WARN("CHB owner %x is not an ACB", this->_owner);
          return blocks::ValidationResult::success();
        }
        if (acb->get_world_permissions().second)
          return blocks::ValidationResult::success();
        if (*acb->owner_key() == key)
          return blocks::ValidationResult::success();
        if (!sig.group_key)
        {
          auto& entries = acb->acl_entries();
          auto it = std::find_if(entries.begin(), entries.end(),
            [&](ACLEntry const& e) { return e.key == key;});
          if (it != entries.end() && it->write)
            return blocks::ValidationResult::success();
        }
        else
        {
          auto& entries = acb->acl_group_entries();
          auto it = std::find_if(entries.begin(), entries.end(),
            [&](ACLEntry const& e) { return e.key == *sig.group_key;});
          if (it != entries.end() && it->write)
          {
            // group has access, now check the key is indeed a group key
            Group g(dht, *sig.group_key);
            auto pubs = g.group_public_keys();
            ELLE_TRACE("checking with group key %s/%s", *sig.group_index,
              pubs.size());
            if (signed(pubs.size()) > *sig.group_index
              && pubs[*sig.group_index] == *sig.signature_key)
              return blocks::ValidationResult::success();
          }
        }
        return blocks::ValidationResult::failure("Key not found");
      }

      Address
      CHB::_hash_address(elle::Buffer const& content,
                         Address owner, elle::Buffer const& salt,
                         elle::Version const& version)
      {
        static elle::Bench bench("bench.chb.hash", 10000_sec);
        elle::Bench::BenchScope bs(bench);
        elle::Buffer saltowner(salt);
        if (version < elle::Version(0, 4, 0))
          owner = Address::null;
        if (owner != Address::null)
          saltowner.append(owner.value(), sizeof(Address::Value));
        elle::IOStream stream(saltowner.istreambuf_combine(content));
        elle::Buffer hash;
        if (content.size() > 262144)
        {
          reactor::background([&] {
              hash = cryptography::hash(stream, cryptography::Oneway::sha256);
            });
        }
        else
          hash = cryptography::hash(stream, cryptography::Oneway::sha256);
        return Address(hash.contents(), flags::immutable_block,
                       version >= elle::Version(0, 5, 0));
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<CHB> _register_chb_serialization("CHB");
    }
  }
}
