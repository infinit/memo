#include <infinit/model/doughnut/Group.hh>

#include <elle/cast.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/GB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/filesystem/umbrella.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Group");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {

      namespace rfs = reactor::filesystem;

      static const elle::Buffer group_block_key = elle::Buffer("group", 5);


      Group::Group(Doughnut& dht, std::string const& name)
        : _dht(dht)
        , _name(name)
      {}

      void
      Group::create()
      {
        infinit::filesystem::umbrella([&] {
            ELLE_DEBUG("create group");
            try
            {
              auto block = _dht.fetch(UB::hash_address(_name));
              if (block)
                throw elle::Error(elle::sprintf("Group %s already exists", _name));
            }
            catch (MissingBlock const&)
            {}
            auto block = _dht.make_block<blocks::GroupBlock>();
            auto gb = elle::cast<GB>::runtime(block);
            auto ub = elle::make_unique<UB>(_name,
              gb->owner_key());
            _dht.store(std::move(ub), STORE_INSERT);
            _dht.store(std::move(gb), STORE_INSERT);
            ELLE_DEBUG("...done");
        });
      }

      cryptography::rsa::PublicKey
      Group::public_control_key()
      {
        auto ub = elle::cast<UB>::runtime(
          _dht.fetch(UB::hash_address(_name)));
        auto key = ub->key();
        ELLE_DEBUG("public_control_key for %s is %s", _name, key);
        return key;
      }

      cryptography::rsa::KeyPair
      Group::_control_key()
      {
        auto pub = public_control_key();
        auto master_block = _dht.fetch(
          OKBHeader::hash_address(pub, group_block_key));
        return elle::serialization::binary::deserialize
          <cryptography::rsa::KeyPair>
          (master_block->data());
      }

      cryptography::rsa::PublicKey
      Group::current_key()
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          ACB::hash_address(key, group_block_key)));
        return block->current_key();
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_members(bool ommit_names)
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          OKBHeader::hash_address(key, group_block_key)));
        auto entries = block->list_permissions(ommit_names);
        std::vector<std::unique_ptr<model::User>> res;
        for (auto& ent: entries)
          res.emplace_back(std::move(ent.user));
        return res;
      }

      void
      Group::add_member(model::User const& user)
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          OKBHeader::hash_address(key, group_block_key)));
        block->add_member(user);
        _dht.store(std::move(block));
      }

      void
      Group::add_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            add_member(*user);
        });
      }

      void
      Group::add_admin(model::User const& user)
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          OKBHeader::hash_address(key, group_block_key)));
        block->add_admin(user);
        _dht.store(std::move(block));
      }

      void
      Group::add_admin(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            add_admin(*user);
        });
      }

      void
      Group::remove_member(model::User const& user)
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          OKBHeader::hash_address(key, group_block_key)));
        block->remove_member(user);
        _dht.store(std::move(block));
      }

      void
      Group::remove_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            remove_member(*user);
        });
      }

      void
      Group::remove_admin(model::User const& user)
      {
        auto key = public_control_key();
        auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
          OKBHeader::hash_address(key, group_block_key)));
        block->remove_admin(user);
        _dht.store(std::move(block));
      }

      void
      Group::remove_admin(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            remove_admin(*user);
        });
      }

      std::vector<cryptography::rsa::KeyPair>
      Group::group_keys()
      {
        return filesystem::umbrella([&] {
            auto key = public_control_key();
            auto block = elle::cast<blocks::GroupBlock>::runtime(_dht.fetch(
              OKBHeader::hash_address(key, group_block_key)));
            return block->all_keys();
        });
      }
    }
  }
}