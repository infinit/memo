#include <infinit/model/doughnut/GB.hh>

#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      GB::GB(Doughnut* owner, cryptography::rsa::KeyPair master)
      : Super(owner, master, elle::Buffer("group", 5))
      , _master_key(master.k())
      {
        keys().emplace(master);
        auto first_group_key = cryptography::rsa::keypair::generate(2048);
        this->_group_public_keys.push_back(first_group_key.K());
        this->_group_keys.push_back(first_group_key);
        auto user_key = owner->keys();
        auto ser_master = elle::serialization::binary::serialize(master.k());
        auto sealed = user_key.K().seal(ser_master);
        auto sk = elle::serialization::binary::serialize(user_key.K());
        this->_ciphered_master_key.push_back(std::make_pair(sk, sealed));
        this->data(elle::serialization::binary::serialize(this->_group_keys));
        this->_acl_changed = true;
        this->set_permissions(user_key.K(), true, false);
      }

      GB::GB(elle::serialization::SerializerIn& s)
      : Super(s)
      {
        s.serialize("group_public_keys", this->_group_public_keys);
        s.serialize("ciphered_master_key", this->_ciphered_master_key);
      }
      void
      GB::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        s.serialize("group_public_keys", this->_group_public_keys);
        s.serialize("ciphered_master_key", this->_ciphered_master_key);
      }

      void
      GB::_sign(elle::serialization::SerializerOut& s) const
      {
        Super::_sign(s);
        s.serialize("master_key", this->_ciphered_master_key);
      }
      void
      GB::_data_sign(elle::serialization::SerializerOut& s) const
      {
        Super::_data_sign(s);
        s.serialize("group_public_keys", this->_group_public_keys);
        s.serialize("master_key", this->_ciphered_master_key);
      }
      cryptography::rsa::PublicKey
      GB::current_key()
      {
        return this->_group_public_keys.back();
      }
      std::vector<cryptography::rsa::KeyPair>
      GB::all_keys()
      {
        if (this->_group_keys.empty())
          this->_extract_group_keys();
        return this->_group_keys;
      }
      void
      GB::_extract_group_keys()
      {
        this->_group_keys = elle::serialization::binary::deserialize<
          decltype(this->_group_keys)>(this->data());
      }
      void
      GB::_extract_master_key()
      {
        auto sk = elle::serialization::binary::serialize(
          doughnut()->keys().K());
        auto it = std::find_if(_ciphered_master_key.begin(),
          _ciphered_master_key.end(), [&](BufferPair const& bp)
          {
            return bp.first == sk;
          });
        if (it != this->_ciphered_master_key.end())
        {
          auto buf = doughnut()->keys().k().open(it->second);
          this->_master_key.emplace(elle::serialization::binary::deserialize<
            cryptography::rsa::PrivateKey>(buf));
          return;
        }
        // look in other keys
        for (auto const& key: this->doughnut()->other_keys())
        {
          sk = elle::serialization::binary::serialize(key.second->K());
          auto it = std::find_if(_ciphered_master_key.begin(),
          _ciphered_master_key.end(), [&](BufferPair const& bp)
          {
            return bp.first == sk;
          });
          if (it != this->_ciphered_master_key.end())
          {
            auto buf = key.second->k().open(it->second);
            this->_master_key.emplace(elle::serialization::binary::deserialize<
              cryptography::rsa::PrivateKey>(buf));
          return;
          }
        }
        throw elle::Error("Access to master key denied.");
      }
      void
      GB::add_member(model::User const& user)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(this->owner_key(),
          *this->_master_key));
        this->_set_permissions(user, true, false);
        this->_acl_changed = true;
      }
      void
      GB::remove_member(model::User const& user)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(this->owner_key(),
          *this->_master_key));
        this->_set_permissions(user, false, false);
        this->_acl_changed = true;
        _extract_group_keys();
        auto new_key = infinit::cryptography::rsa::keypair::generate(2048);
        this->_group_public_keys.push_back(new_key.K());
        this->_group_keys.push_back(new_key);
        this->data(elle::serialization::binary::serialize(this->_group_keys));
      }
      void
      GB::add_admin(model::User const& user_)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(this->owner_key(),
          *this->_master_key));
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto sk = elle::serialization::binary::serialize(user.key());
          auto it = std::find_if(_ciphered_master_key.begin(),
          _ciphered_master_key.end(), [&](BufferPair const& bp)
          {
            return bp.first == sk;
          });
          if (it != this->_ciphered_master_key.end())
            return;
          auto ser_master = elle::serialization::binary::serialize(*this->_master_key);
          this->_ciphered_master_key.push_back(std::make_pair(sk,
            user.key().seal(ser_master)));
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
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(this->owner_key(),
          *this->_master_key));
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto sk = elle::serialization::binary::serialize(user.key());
          auto it = std::find_if(_ciphered_master_key.begin(),
          _ciphered_master_key.end(), [&](BufferPair const& bp)
          {
            return bp.first == sk;
          });
          if (it == this->_ciphered_master_key.end())
            throw elle::Error("No such user in admin list");
          this->_ciphered_master_key.erase(it);
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
        for (auto const& p: this->_ciphered_master_key)
        {
          std::unique_ptr<model::User> user;
          auto key = elle::serialization::binary::deserialize
            <cryptography::rsa::PublicKey>(p.first);
          if (ommit_names)
            user.reset(new doughnut::User(this->owner_key(), ""));
          else
            user = this->doughnut()->make_user(
                elle::serialization::serialize
                <cryptography::rsa::PublicKey, elle::serialization::Json>(
                  key));
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
      , _group_public_keys(other._group_public_keys)
      , _ciphered_master_key(other._ciphered_master_key)
      {}
      GB::~GB()
      {}

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<GB> _register_gb_serialization("GB");
    }
  }
}