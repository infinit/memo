#include <infinit/model/doughnut/Group.hh>

#include <elle/algorithm.hh>
#include <elle/cast.hh>

#include <elle/cryptography/hash.hh>
#include <elle/cryptography/rsa/KeyPair.hh>
#include <elle/cryptography/rsa/PublicKey.hh>

#include <infinit/filesystem/umbrella.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/GB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/conflict/UBUpserter.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Group");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace rfs = elle::reactor::filesystem;

      static const elle::Buffer group_block_key = elle::Buffer("group", 5);

      elle::reactor::LocalStorage<std::vector<elle::cryptography::rsa::PublicKey>>
      Group::_stack;

      Group::Group(Doughnut& dht, std::string const& name)
        : _dht(dht)
        , _name(name)
        , _public_control_key()
        , _block()
      {}

      Group::Group(Doughnut& dht, elle::cryptography::rsa::PublicKey k)
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
        if (this->_stack.get().back() != this->_public_control_key)
          ELLE_WARN("Group stack error");
        else
          this->_stack.get().pop_back();
      }

      void
      Group::_stack_push()
      {
        auto& s = this->_stack.get();
        if (elle::contains(s, this->_public_control_key))
          elle::err("Group loop");
        else
          s.push_back(*this->_public_control_key);
      }

      struct GroupBlockInserter
        : public DummyConflictResolver
      {
        using Super = infinit::model::DummyConflictResolver;

        GroupBlockInserter(std::string const& name)
          : Super()
          , _name(name)
        {}

        GroupBlockInserter(elle::serialization::Serializer& s,
                           elle::Version const& version)
          : Super()
        {
          this->serialize(s, version);
        }

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override
        {
          Super::serialize(s, version);
          s.serialize("name", this->_name);
        }

        std::string
        description() const override
        {
          return elle::sprintf("write group block for %s", this->_name);
        }

      private:
        ELLE_ATTRIBUTE(std::string, name);
      };

      static const elle::serialization::Hierarchy<ConflictResolver>::
      Register<GroupBlockInserter> _register_group_block_inserter(
        "GroupBlockInserter");

      void
      Group::create()
      {
        ELLE_TRACE_SCOPE("%s: create", this->_name);
        infinit::filesystem::umbrella([&] {
            try
            {
              auto block =
                this->_dht.fetch(UB::hash_address(_name, this->_dht));
              if (block)
                elle::err("Group %s already exists", _name);
            }
            catch (MissingBlock const&)
            {}
            auto block = _dht.make_block<blocks::GroupBlock>();
            auto gb = elle::cast<GB>::runtime(block);
            ELLE_TRACE("New group block address: %x, key %s",
                       gb->address(), gb->owner_key());
            auto ub = std::make_unique<UB>(&_dht, _name,
              *gb->owner_key());
            auto rub = std::make_unique<UB>(&_dht, "@"+_name,
              *gb->owner_key(), true);
            auto hub = std::make_unique<UB>(
              &_dht, ':' + UB::hash(*gb->owner_key()).string(), *gb->owner_key());
            // FIXME
            _dht.insert(std::move(hub),
                        std::make_unique<UserBlockUpserter>(
                          elle::sprintf("@%s", this->_name)));
            _dht.insert(std::move(ub),
                        std::make_unique<UserBlockUpserter>(
                          elle::sprintf("@%s", this->_name)));
            _dht.insert(std::move(rub),
                        std::make_unique<ReverseUserBlockUpserter>(
                          elle::sprintf("@%s", this->_name)));
            _dht.insert(std::move(gb),
                        std::make_unique<GroupBlockInserter>(
                          elle::sprintf("@%s", this->_name)));
        });
      }

      void
      Group::destroy()
      {
        infinit::filesystem::umbrella([&] {
          if (_name.empty())
            elle::err("Group destruction needs group name as input");
          public_control_key();
          block();
          auto const ctrl = _control_key();
          // UB
          {
            auto addr = UB::hash_address(this->_name, this->_dht);
            auto block = _dht.fetch(addr);
            auto to_sign = elle::serialization::binary::serialize((blocks::Block*)block.get());
            blocks::RemoveSignature sig;
            sig.signature_key.emplace(ctrl.K());
            sig.signature.emplace(ctrl.k().sign(to_sign));
            this->_dht.remove(addr, sig);
          }
          // RUB
          {
            auto addr = UB::hash_address(*this->_public_control_key, this->_dht);
            auto block = _dht.fetch(addr);
            auto to_sign = elle::serialization::binary::serialize((blocks::Block*)block.get());
            blocks::RemoveSignature sig;
            sig.signature_key.emplace(ctrl.K());
            sig.signature.emplace(ctrl.k().sign(to_sign));
            this->_dht.remove(addr, sig);
          }
          this->_dht.remove(this->_block->address(),
                            this->_block->sign_remove(this->_dht));
        });
      }

      elle::cryptography::rsa::PublicKey
      Group::public_control_key() const
      {
        if (!this->_public_control_key)
        {
          ELLE_TRACE_SCOPE("%s: fetch", this->_name);
          auto ub = elle::cast<UB>::runtime(
            this->_dht.fetch(UB::hash_address(_name, this->_dht)));
          elle::unconst(this)->_public_control_key.emplace(ub->key());
          elle::unconst(this)->_stack_push();
          ELLE_DEBUG("public_control_key for %s is %s",
                     this->_name, this->_public_control_key);
        }
        return *this->_public_control_key;
      }

      GB const&
      Group::block() const
      {
        if (this->_block)
          return *this->_block;
        ELLE_TRACE_SCOPE("%s: fetch block", *this);
        auto key = this->public_control_key();
        try
        {
          static
          std::unordered_map<elle::cryptography::rsa::PublicKey, Address>
          address_cache;

          Address addr;
          auto it = address_cache.find(key);
          if (it == address_cache.end())
          {
            addr = ACB::hash_address(this->_dht, key, group_block_key);
            address_cache.insert(std::make_pair(key, addr));
          }
          else
            addr = it->second;
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
            ELLE_WARN(
              "group block %s has an obsolete address and requires migration",
              addr);
            return *this->_block;
          }
          catch (MissingBlock const&)
          {}
          throw;
        }
        catch (elle::Error const& e)
        {
          ELLE_TRACE("exception fetching GB: %s", e.what());
          throw;
        }
      }

      GB&
      Group::block()
      {
        GB const& res = static_cast<const Group*>(this)->block();
        return elle::unconst(res);
      }

      elle::cryptography::rsa::KeyPair
      Group::_control_key()
      {
        if (auto priv = this->block().control_key())
          return {public_control_key(), *priv};
        else
          elle::err("You are not a group admin");
      }

      elle::cryptography::rsa::PublicKey
      Group::current_public_key() const
      {
        return this->block().current_public_key();
      }

      int
      Group::version() const
      {
        return this->block().group_version();
      }

      elle::cryptography::rsa::KeyPair
      Group::current_key() const
      {
        return this->block().current_key();
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_members(bool)
      {
        auto entries = this->block().list_permissions(_dht);
        auto res = std::vector<std::unique_ptr<model::User>>{};
        for (auto& ent: entries)
          res.emplace_back(std::move(ent.user));
        return res;
      }

      std::vector<std::unique_ptr<model::User>>
      Group::list_admins(bool omit_names)
      {
        return this->block().list_admins(omit_names);
      }

      void
      Group::add_member(model::User const& user)
      {
        this->block().add_member(user);
        this->_dht.seal_and_update(this->block(),
                                   std::make_unique<GroupConflictResolver>(
                                     GroupConflictResolver::Action::add_member,
                                     user));
      }

      void
      Group::add_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT();
            this->add_member(*user);
        });
      }

      void
      Group::add_admin(model::User const& user)
      {
        this->block().add_admin(user);
        this->_dht.seal_and_update(this->block(),
                                   std::make_unique<GroupConflictResolver>(
                                     GroupConflictResolver::Action::add_admin,
                                     user));
      }

      void
      Group::add_admin(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT();
            add_admin(*user);
        });
      }

      void
      Group::remove_member(model::User const& user)
      {
        this->block().remove_member(user);
        this->_dht.seal_and_update(
          this->block(),
          std::make_unique<GroupConflictResolver>(
            GroupConflictResolver::Action::remove_member,
            user));
      }

      void
      Group::remove_member(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT();
            this->remove_member(*user);
        });
      }

      void
      Group::remove_admin(model::User const& user)
      {
        this->block().remove_admin(user);
        this->_dht.seal_and_update(
          this->block(),
          std::make_unique<GroupConflictResolver>(
            GroupConflictResolver::Action::remove_admin,
            user));
      }

      void
      Group::remove_admin(elle::Buffer const& userdata)
      {
        infinit::filesystem::umbrella([&] {
            auto user = _dht.make_user(userdata);
            if (!user)
              THROW_NOENT();
            this->remove_admin(*user);
        });
      }

      std::vector<elle::cryptography::rsa::KeyPair>
      Group::group_keys()
      {
        return filesystem::umbrella([&] {
            return this->block().all_keys();
        });
      }

      std::vector<elle::cryptography::rsa::PublicKey>
      Group::group_public_keys()
      {
        return filesystem::umbrella([&] {
            return this->block().all_public_keys();
        });
      }

      boost::optional<std::string> const&
      Group::description() const
      {
        // WORKAROUND: Force the lambda return type or else umbrella
        // breaks it.  This is at least the case with clang.
        return infinit::filesystem::umbrella(
          [&] () -> boost::optional<std::string> const&
          {
            return this->block().description();
          });
      }

      void
      Group::description(boost::optional<std::string> const& description)
      {
        infinit::filesystem::umbrella(
          [&]
          {
            this->block().description(description);
            this->_dht.seal_and_update(
              this->block(),
              std::make_unique<GroupConflictResolver>(
                GroupConflictResolver::Action::set_description, description));
          });
      }

      void
      Group::print(std::ostream& o) const
      {
        elle::fprintf(o, "%s(\"%s\", \"%s\")", elle::type_info(*this),
                      short_key_hash(this->public_control_key()), this->name());
      }

      GroupConflictResolver::GroupConflictResolver(GroupConflictResolver&& b)
        : _action(b._action)
        , _key(std::move(b._key))
        , _name(std::move(b._name))
      {}

      GroupConflictResolver::GroupConflictResolver(elle::serialization::SerializerIn& s,
                                                   elle::Version const& v)
      {
        serialize(s, v);
      }

      GroupConflictResolver::~GroupConflictResolver()
      {}

      void
      GroupConflictResolver::serialize(elle::serialization::Serializer& s,
                                       elle::Version const& v)
      {
        s.serialize("action", this->_action, elle::serialization::as<int>());
        s.serialize("key", this->_key);
        s.serialize("name", this->_name);
        s.serialize("description", this->_description);
      }

      GroupConflictResolver::GroupConflictResolver(Action action,
                                                   model::User const& user)
      {
        ELLE_ASSERT_NEQ(action, Action::set_description);
        auto duser = dynamic_cast<doughnut::User const*>(&user);
        if (!duser)
          elle::err("User argument is not a doughnut user");
        this->_action = action;
        this->_key = std::make_unique<elle::cryptography::rsa::PublicKey>(duser->key());
        this->_name = duser->name();
        this->_description = boost::none;
      }

      GroupConflictResolver::GroupConflictResolver(
        Action action,
        boost::optional<std::string> description)
        : _action(action)
        , _key(nullptr)
        , _name()
        , _description(std::move(description))
      {
        ELLE_ASSERT_EQ(this->_action, Action::set_description);
      }

      std::unique_ptr<blocks::Block>
      GroupConflictResolver::operator()(blocks::Block& block,
                                        blocks::Block& current)
      {
        ELLE_TRACE("Conflict editing group, replaying action on %s", this->_name);
        auto res = elle::cast<GB>::runtime(current.clone());
        if (!res)
          elle::err("GroupConflictResolver failed to access current block");
        std::unique_ptr<doughnut::User> user(nullptr);
        if (this->_name)
          user.reset(new doughnut::User(*this->_key, this->_name.get()));
        switch (this->_action)
        {
        case Action::add_member:
          ELLE_ASSERT_NEQ(user, nullptr);
          res->add_member(*user);
          break;
        case Action::remove_member:
          ELLE_ASSERT_NEQ(user, nullptr);
          res->remove_member(*user);
          break;
        case Action::add_admin:
          ELLE_ASSERT_NEQ(user, nullptr);
          res->add_admin(*user);
          break;
        case Action::remove_admin:
          ELLE_ASSERT_NEQ(user, nullptr);
          res->remove_admin(*user);
          break;
        case Action::set_description:
          ELLE_ASSERT_EQ(user, nullptr);
          res->description(this->_description);
          break;
        }
        return std::move(res);
      }

      std::string
      GroupConflictResolver::description() const
      {
        if (this->_name)
        {
          // User is not necessary a "User", it can be a Group.
          doughnut::User user(*this->_key, this->_name.get());
          return elle::sprintf("%s \"%s\" to/from group", this->_action,
                               user.name());
        }
        else
        {
          return elle::sprintf("%s to %s", this->_action, this->_description);
        }
      }

      std::ostream&
      operator << (std::ostream& out, GroupConflictResolver::Action action)
      {
        switch (action)
        {
        case GroupConflictResolver::Action::add_member:
          return out << "add member";
        case GroupConflictResolver::Action::remove_member:
          return out << "remove member";
        case GroupConflictResolver::Action::add_admin:
          return out << "add admin";
        case GroupConflictResolver::Action::remove_admin:
          return out << "remove admin";
        case GroupConflictResolver::Action::set_description:
          return out << "set description";
        }
        elle::unreachable();
      }

      static const elle::serialization::Hierarchy<model::ConflictResolver>::
      Register<GroupConflictResolver> _register_gcr("gcr");
    }
  }
}
