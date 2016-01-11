#include <infinit/model/doughnut/GB.hh>

#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.GB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      GB::GB(Doughnut* owner,
             cryptography::rsa::KeyPair master)
        : Super(owner, {}, elle::Buffer("group", 5), master)
      {
        ELLE_TRACE_SCOPE("%s: create", *this);
        auto first_group_key = cryptography::rsa::keypair::generate(2048);
        this->_public_keys.push_back(first_group_key.K());
        this->_keys.push_back(first_group_key);
        auto user_key = owner->keys();
        auto ser_master = elle::serialization::binary::serialize(master.k());
        auto sealed = user_key.K().seal(ser_master);
        this->_admin_keys.emplace(user_key.K(), sealed);
        this->data(elle::serialization::binary::serialize(this->_keys));
        this->_acl_changed = true;
        this->set_permissions(user_key.K(), true, false);
      }

      GB::GB(elle::serialization::SerializerIn& s,
             elle::Version const& version)
        : Super(s, version)
      {
        ELLE_TRACE_SCOPE("%s: deserialize", *this);
        this->_serialize(s, version);
        // Extract owner key if possible
        auto const& keys = this->doughnut()->keys();
        auto it = this->_admin_keys.find(keys.K());
        if (it != this->_admin_keys.end())
        {
          ELLE_DEBUG("we are group admin");
          this->_owner_private_key =
            std::make_shared(elle::serialization::binary::deserialize<
                             cryptography::rsa::PrivateKey>(
                               keys.k().open(it->second)));
        }
        else
          ELLE_DEBUG("we are not group admin");
      }

      GB::~GB()
      {}

      void
      GB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s, version);
        this->_serialize(s, version);
      }

      void
      GB::_serialize(elle::serialization::Serializer& s,
                    elle::Version const&)
      {
        s.serialize("public_keys", this->_public_keys);
        s.serialize("admin_keys", this->_admin_keys);
      }

      GB::OwnerSignature::OwnerSignature(GB const& b)
        : Super::OwnerSignature(b)
        , _block(b)
      {}

      void
      GB::OwnerSignature::_serialize(elle::serialization::SerializerOut& s,
                                     elle::Version const& v)
      {
        ELLE_ASSERT_GTE(v, elle::Version(0, 4, 0));
        GB::Super::OwnerSignature::_serialize(s, v);
        s.serialize("admins", this->_block._admin_keys);
      }

      std::unique_ptr<typename BaseOKB<blocks::GroupBlock>::OwnerSignature>
      GB::_sign() const
      {
        return elle::make_unique<OwnerSignature>(*this);
      }

      GB::DataSignature::DataSignature(GB const& block)
        : GB::Super::DataSignature(block)
        , _block(block)
      {}

      void
      GB::DataSignature::serialize(elle::serialization::Serializer& s_,
                                   elle::Version const& v)
      {
        // FIXME: Improve when split-serialization is added.
        ELLE_ASSERT(s_.out());
        auto& s = reinterpret_cast<elle::serialization::SerializerOut&>(s_);
        GB::Super::DataSignature::serialize(s, v);
        s.serialize("public_keys", this->_block.public_keys());
      }

      std::unique_ptr<GB::Super::DataSignature>
      GB::_data_sign() const
      {
        return elle::make_unique<DataSignature>(*this);
      }

      cryptography::rsa::PublicKey
      GB::current_public_key()
      {
        return this->_public_keys.back();
      }

      cryptography::rsa::KeyPair
      GB::current_key()
      {
        if (this->_keys.empty())
          this->_extract_keys();
        return this->_keys.back();
      }

      int
      GB::version()
      {
        return this->_public_keys.size();
      }

      std::vector<cryptography::rsa::KeyPair>
      GB::all_keys()
      {
        if (this->_keys.empty())
          this->_extract_keys();
        return this->_keys;
      }

      std::vector<cryptography::rsa::PublicKey>
      GB::all_public_keys()
      {
        return this->_public_keys;
      }

      void
      GB::_extract_keys()
      {
        this->_keys = elle::serialization::binary::deserialize<
          decltype(this->_keys)>(this->data());
      }

      void
      GB::add_member(model::User const& user)
      {
        this->_set_permissions(user, true, false);
        this->_acl_changed = true;
      }
      void
      GB::remove_member(model::User const& user)
      {
        this->_set_permissions(user, false, false);
        this->_acl_changed = true;
        _extract_keys();
        auto new_key = infinit::cryptography::rsa::keypair::generate(2048);
        this->_public_keys.push_back(new_key.K());
        this->_keys.push_back(new_key);
        this->data(elle::serialization::binary::serialize(this->_keys));
      }

      void
      GB::add_admin(model::User const& user_)
      {
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          if (contains(this->_admin_keys, user.key()))
            return;
          auto ser_master = elle::serialization::binary::serialize(
            *this->_owner_private_key);
          this->_admin_keys.emplace(user.key(), user.key().seal(ser_master));
          this->_acl_changed = true;
        }
        catch (std::bad_cast const&)
        {
          throw elle::Error("doughnut was passed a non-doughnut user.");
        }
      }

      void
      GB::remove_admin(model::User const& user_)
      {
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto it = this->_admin_keys.find(user.key());
          if (it == this->_admin_keys.end())
            throw elle::Error(elle::sprintf("no such admin: %s", user.key()));
          this->_admin_keys.erase(it);
          this->_acl_changed = true;
        }
        catch (std::bad_cast const&)
        {
          throw elle::Error("doughnut was passed a non-doughnut user.");
        }
      }

      std::vector<std::unique_ptr<model::User>>
      GB::list_admins(bool ommit_names)
      {
        std::vector<std::unique_ptr<model::User>> res;
        for (auto const& key: this->_admin_keys)
        {
          std::unique_ptr<model::User> user;
          if (ommit_names)
            user.reset(new doughnut::User(*this->owner_key(), ""));
          else
            user = this->doughnut()->make_user(
              elle::serialization::json::serialize(key.second));
          res.emplace_back(std::move(user));
        }
        return res;
      }

      std::unique_ptr<blocks::Block>
      GB::clone(bool sealed_copy) const
      {
        return std::unique_ptr<blocks::Block>(new Self(*this, sealed_copy));
      }

      GB::GB(const GB& other, bool sealed_copy)
        : Super(other, sealed_copy)
        , _public_keys(other._public_keys)
        , _admin_keys(other._admin_keys)
      {}

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<GB> _register_gb_serialization("GB");

      /*----------.
      | Printable |
      `----------*/

      void
      GB::print(std::ostream& ouptut) const
      {
        elle::fprintf(ouptut, "%s(%f)",
                      elle::type_info<GB>(), *this->owner_key());
      }
    }
  }
}
