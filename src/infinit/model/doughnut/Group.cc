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
      reactor::LocalStorage<std::vector<cryptography::rsa::PublicKey>>
      Group::_stack;

      Group::Group(Doughnut& dht, std::string const& name)
        : _dht(dht)
        , _name(name)
        , _public_control_key()
        , _block()
      {}

      Group::Group(Doughnut& dht, cryptography::rsa::PublicKey k)
        : _dht(dht)
        , _name()
        , _public_control_key(k)
        , _block()
      {
        this->_stack_push();
      }

      Group::~Group()
      {
        if (!this->_public_control_key)
          return;
        if (this->_stack.Get().back() != this->_public_control_key)
          ELLE_WARN("Group stack error");
        else
          this->_stack.Get().pop_back();
      }

      void
      Group::_stack_push()
      {
        auto& s = this->_stack.Get();
        if (std::find(s.begin(), s.end(), this->_public_control_key) != s.end())
          throw elle::Error("Group loop");
        s.push_back(*this->_public_control_key);
      }

      void
      Group::create()
      {
        ELLE_TRACE_SCOPE("%s: create", *this);
        infinit::filesystem::umbrella([&] {
            try
            {
              auto block =
                this->_dht.fetch(UB::hash_address(_name, this->_dht));
              if (block)
                throw elle::Error(elle::sprintf("Group %s already exists", _name));
            }
            catch (MissingBlock const&)
            {}
            auto block = _dht.make_block<blocks::GroupBlock>();
            auto gb = elle::cast<GB>::runtime(block);
            ELLE_TRACE("New group block address: %x, key %s",
                       gb->address(), gb->owner_key());
            auto ub = elle::make_unique<UB>(&_dht, _name,
              *gb->owner_key());
            auto rub = elle::make_unique<UB>(&_dht, "@"+_name,
              *gb->owner_key(), true);
            _dht.store(std::move(ub), STORE_INSERT);
            _dht.store(std::move(rub), STORE_INSERT);
            _dht.store(std::move(gb), STORE_INSERT);
            ELLE_DEBUG("...done");
        });
      }

      void
      Group::destroy()
      {
        infinit::filesystem::umbrella([&] {
          if (_name.empty())
            throw elle::Error("Group destruction needs group name as input");
          public_control_key();
          block();
          auto addr = UB::hash_address(this->_name, this->_dht);
          auto raddr = UB::hash_address(*this->_public_control_key, this->_dht);
          this->_dht.remove(addr);
          this->_dht.remove(raddr);
          this->_dht.remove(this->_block->address(),
                            this->_block->sign_remove(this->_dht));
        });
      }

      cryptography::rsa::PublicKey
      Group::public_control_key() const
      {
        if (this->_public_control_key)
          return *_public_control_key;
        ELLE_TRACE_SCOPE("%s: fetch", *this);
        auto ub = elle::cast<UB>::runtime(
          this->_dht.fetch(UB::hash_address(_name, this->_dht)));
        elle::unconst(this)->_public_control_key.emplace(ub->key());
        elle::unconst(this)->_stack_push();
        ELLE_DEBUG("public_control_key for %s is %s",
                   this->_name, this->_public_control_key);
        return *this->_public_control_key;
      }

      GB&
      Group::block() const
      {
        if (this->_block)
          return *this->_block;
        ELLE_TRACE_SCOPE("%s: fetch block", *this);
        auto key = this->public_control_key();
        try
        {
          auto addr = ACB::hash_address(this->_dht, key, group_block_key);
          elle::unconst(this)->_block = elle::cast<GB>::runtime(
            this->_dht.fetch(addr));
          return *this->_block;
        }
        catch (MissingBlock const&)
        {
          ELLE_TRACE_SCOPE("missing group block, retry with pre 0.5 address");
          try
          {
            auto addr = ACB::hash_address(key, group_block_key,
                                          elle::Version(0, 4, 0));
            elle::unconst(this)->_block = elle::cast<GB>::runtime(
              this->_dht.fetch(addr));
            return *this->_block;
          }
          catch (MissingBlock const&)
          {}
          throw;
        }
      }

      cryptography::rsa::KeyPair
      Group::_control_key()
      {
        return elle::serialization::binary::deserialize
          <cryptography::rsa::KeyPair>(this->block().data());
      }

      cryptography::rsa::PublicKey
      Group::current_public_key() const
      {
        return this->block().current_public_key();
      }

      int
      Group::version() const
      {
        return this->block().group_version();
      }

      cryptography::rsa::KeyPair
      Group::current_key() const
      {
        return this->block().current_key();
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_members(bool ommit_names)
      {
        auto entries = this->block().list_permissions(_dht);
        std::vector<std::unique_ptr<model::User>> res;
        for (auto& ent: entries)
          res.emplace_back(std::move(ent.user));
        return res;
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_admins(bool ommit_names)
      {
        auto entries = this->block().list_admins(ommit_names);
        return entries;
      }

      void
      Group::add_member(model::User const& user)
      {
        this->block().add_member(user);
        this->_dht.store(this->block());
      }

      void
      Group::add_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            this->add_member(*user);
        });
      }

      void
      Group::add_admin(model::User const& user)
      {
        this->block().add_admin(user);
        this->_dht.store(this->block());
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
        this->block().remove_member(user);
        this->_dht.store(this->block());
      }

      void
      Group::remove_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            this->remove_member(*user);
        });
      }

      void
      Group::remove_admin(model::User const& user)
      {
        this->block().remove_admin(user);
        this->_dht.store(this->block());
      }

      void
      Group::remove_admin(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT;
            this->remove_admin(*user);
        });
      }

      std::vector<cryptography::rsa::KeyPair>
      Group::group_keys()
      {
        return filesystem::umbrella([&] {
            return this->block().all_keys();
        });
      }

      std::vector<cryptography::rsa::PublicKey>
      Group::group_public_keys()
      {
        return filesystem::umbrella([&] {
            return this->block().all_public_keys();
        });
      }
    }
  }
}
