#include <infinit/model/doughnut/Group.hh>

#include <elle/cast.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/UB.hh>

#include <infinit/filesystem/umbrella.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Group");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {

      static const elle::Buffer group_public_block_name
        = elle::Buffer::make("keysPublic");
      static const elle::Buffer group_private_block_name
        = elle::Buffer::make("keysPrivate");
      static const elle::Buffer group_master_block_name
        = elle::Buffer::make("masterKey");

      Group::Group(Doughnut& dht, std::string const& name)
        : _dht(dht)
        , _name(name)
      {}

      void
      Group::create()
      {
        auto control_keys = cryptography::rsa::keypair::generate(2048);
        { // store the control key in an ACB with read access to current user
          auto block_master = elle::make_unique<ACB>(&_dht, control_keys,
            group_master_block_name);
          block_master->data(elle::serialization::binary::serialize(control_keys));
          block_master->set_permissions(_dht.keys().K(), true, false);
          _dht.store(std::move(block_master));
        }
        { // make a UB to map name -> key
          auto ub_control = elle::make_unique<UB>(_name, control_keys.K());
          _dht.store(std::move(ub_control));
        }
        // make the first group key
        auto first_key = cryptography::rsa::keypair::generate(2048);
        auto first_pub = first_key.K();
        { // make a private ACB that will contain the group keypairs
          // user with R access to it are the group members
          auto block_keypairs = elle::make_unique<ACB>(&_dht, control_keys,
            group_private_block_name);
          block_keypairs->set_permissions(_dht.keys().K(), true, false);
          GroupPrivateBlockContent content;
          content.emplace_back(std::move(first_key));
          block_keypairs->data(elle::serialization::binary::serialize(content));
          ELLE_DEBUG("creating private keys block at %x", block_keypairs->address());
          _dht.store(std::move(block_keypairs));
        }
        { // Make a world-readable ACB that will contain the group public keys
          auto block_pub = elle::make_unique<ACB>(&_dht, control_keys,
            group_public_block_name);
          block_pub->set_world_permissions(true, false);
          GroupPublicBlockContent content;
          content.push_back(first_pub);
          block_pub->data(elle::serialization::binary::serialize(content));
          ELLE_DEBUG("creating public keys block at %x", block_pub->address());
          _dht.store(std::move(block_pub));
        }
      }

      cryptography::rsa::PublicKey
      Group::public_control_key()
      {
        ELLE_DEBUG("public_control_key for %s", _name);
        auto ub = elle::cast<UB>::runtime(
          _dht.fetch(UB::hash_address(_name)));
        auto key = ub->key();
        return key;
      }

      cryptography::rsa::KeyPair
      Group::_control_key()
      {
        auto pub = public_control_key();
        auto master_block = _dht.fetch(
          ACB::hash_address(pub, group_master_block_name));
        return elle::serialization::binary::deserialize
          <cryptography::rsa::KeyPair>
          (master_block->data());
      }

      cryptography::rsa::PublicKey
      Group::current_key()
      {
        auto key = public_control_key();
        auto pub_keys_block = _dht.fetch(
          ACB::hash_address(key, group_public_block_name));
        auto content = elle::serialization::binary::deserialize
          <GroupPublicBlockContent>(
            pub_keys_block->data());
        ELLE_TRACE("current group key for %s: %s", _name, content.back());
        return content.back();
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_members(bool ommit_names)
      {
        auto key = public_control_key();
        ELLE_DEBUG("fetch private key block for %s", _name);
        auto priv_key_block = elle::cast<ACB>::runtime(_dht.fetch(
          ACB::hash_address(key, group_private_block_name)));
        ELLE_DEBUG("list permissions for %s", _name);
        auto entries = priv_key_block->list_permissions(ommit_names);
        std::vector<std::unique_ptr<model::User>> res;
        for (auto& ent: entries)
          res.emplace_back(std::move(ent.user));
        return res;
      }

      void
      Group::add_member(model::User const& user)
      {
        ELLE_DEBUG("add member %s to %s", elle::unconst(user).name(), _name);
        infinit::filesystem::umbrella([&] {
            auto key = _control_key();
            auto priv_key_block = elle::cast<ACB>::runtime(_dht.fetch(
              ACB::hash_address(key.K(), group_private_block_name)));
            priv_key_block->keys().emplace(std::move(key));
            priv_key_block->ACLBlock::set_permissions(user, true, false);
            _dht.store(std::move(priv_key_block));
        });
      }

      void
      Group::remove_member(model::User const& user)
      {
        ELLE_DEBUG("remove member %s from %s", elle::unconst(user).name(), _name);
        infinit::filesystem::umbrella([&] {
          auto key = _control_key();
          auto priv_key_block = elle::cast<ACB>::runtime(_dht.fetch(
            ACB::hash_address(key.K(), group_private_block_name)));
          auto pub_key_block = elle::cast<ACB>::runtime(_dht.fetch(
            ACB::hash_address(key.K(), group_public_block_name)));
          pub_key_block->keys().emplace(key);
          priv_key_block->keys().emplace(key);
          priv_key_block->ACLBlock::set_permissions(user, false, false);

          auto new_key = cryptography::rsa::keypair::generate(2048);
          auto pub_content = elle::serialization::binary::deserialize
            <GroupPublicBlockContent>(
              pub_key_block->data());
          pub_content.push_back(new_key.K());
          pub_key_block->data(elle::serialization::binary::serialize(pub_content));

          auto priv_content = elle::serialization::binary::deserialize
            <GroupPrivateBlockContent>(
              priv_key_block->data());
          priv_content.push_back(new_key);
          priv_key_block->data(elle::serialization::binary::serialize(priv_content));
          _dht.store(std::move(pub_key_block));
          _dht.store(std::move(priv_key_block));
         });
      }

      std::vector<cryptography::rsa::KeyPair>
      Group::group_keys()
      {
        auto key = public_control_key();
        auto priv_key_block = elle::cast<ACB>::runtime(_dht.fetch(
          ACB::hash_address(key, group_private_block_name)));
        auto priv_content = elle::serialization::binary::deserialize
          <GroupPrivateBlockContent>(
            priv_key_block->data());
        ELLE_DEBUG("Returning %s keys for %s", priv_content.size(), _name);
        return priv_content;
      }
    }
  }
}