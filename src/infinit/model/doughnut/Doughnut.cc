#include <infinit/model/doughnut/Doughnut.hh>

#include <boost/optional.hpp>

#include <elle/Buffer.hh>
#include <elle/Error.hh>
#include <elle/IOStream.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh> // FIXME

#include <reactor/Scope.hh>
#include <reactor/exception.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

#include <infinit/model/doughnut/CHB.cc>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Doughnut::Doughnut(cryptography::rsa::KeyPair keys,
                         cryptography::rsa::PublicKey owner,
                         Passport passport,
                         std::unique_ptr<overlay::Overlay> overlay,
                         std::shared_ptr<Local> local,
                         std::unique_ptr<Consensus> consensus)
        : _overlay(std::move(overlay))
        , _consensus(std::move(consensus))
        , _keys(std::move(keys))
        , _owner(std::move(owner))
        , _passport(std::move(passport))
      {
        if (!this->_consensus)
          this->_consensus = elle::make_unique<Consensus>(*this);
        this->overlay()->doughnut(this);
        if (local)
        {
          local->doughnut().reset(this);
          this->overlay()->register_local(local);
        }
      }

      Doughnut::Doughnut(std::string name,
                         cryptography::rsa::KeyPair keys,
                         cryptography::rsa::PublicKey owner,
                         Passport passport,
                         std::unique_ptr<overlay::Overlay> overlay,
                         std::shared_ptr<Local> local,
                         std::unique_ptr<Consensus> consensus)
        : Doughnut(std::move(keys),
                   std::move(owner),
                   std::move(passport),
                   std::move(overlay),
                   std::move(local),
                   std::move(consensus))
      {
        try
        {
          ELLE_TRACE_SCOPE("%s: check user block", *this);
          auto block = this->fetch(UB::hash_address(name));
          ELLE_DEBUG("%s: user block for %s already present at %x",
                     *this, name, block->address());
          auto ub = elle::cast<UB>::runtime(block);
          if (ub->key() != this->keys().K())
            throw elle::Error(
              elle::sprintf("user block exists at %s(%x) with different key",
                            name, UB::hash_address(name)));
        }
        catch (MissingBlock const&)
        {
          UB user(name, this->keys().K());
          ELLE_TRACE_SCOPE("%s: store user block at %x for %s",
                           *this, user.address(), name);
          this->store(user);
        }
        try
        {
          ELLE_TRACE_SCOPE("%s: check user reverse block", *this);
          auto block = this->fetch(UB::hash_address(this->keys().K()));
          ELLE_DEBUG("%s: user reverse block for %s already present at %x",
                     *this, name, block->address());
          auto ub = elle::cast<UB>::runtime(block);
          if (ub->name() != name)
            throw elle::Error(
              elle::sprintf(
                "user reverse block exists at %s(%x) with different name: %s",
                name, UB::hash_address(this->keys().K()), ub->name()));
        }
        catch(MissingBlock const&)
        {
          UB user(name, this->keys().K(), true);
          ELLE_TRACE_SCOPE("%s: store reverse user block at %x", *this,
                           user.address());
          this->store(user);
        }
      }

      Doughnut::~Doughnut()
      {}

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create OKB", *this);
        return elle::make_unique<OKB>(const_cast<Doughnut*>(this));
      }

      std::unique_ptr<blocks::ImmutableBlock>
      Doughnut::_make_immutable_block(elle::Buffer content) const
      {
        ELLE_TRACE_SCOPE("%s: create CHB", *this);
        return elle::make_unique<CHB>(std::move(content));
      }

      std::unique_ptr<blocks::ACLBlock>
      Doughnut::_make_acl_block() const
      {
        ELLE_TRACE_SCOPE("%s: create ACB", *this);
        return elle::make_unique<ACB>(const_cast<Doughnut*>(this));
      }

      std::unique_ptr<model::User>
      Doughnut::_make_user(elle::Buffer const& data) const
      {
        if (data.size() == 0)
          throw elle::Error("invalid empty user");
        if (data[0] == '{')
        {
          ELLE_TRACE_SCOPE("%s: fetch user from public key", *this);
          elle::IOStream input(data.istreambuf());
          elle::serialization::json::SerializerIn s(input);
          cryptography::rsa::PublicKey pub(s);
          try
          {
            auto block = this->fetch(UB::hash_address(pub));
            auto ub = elle::cast<UB>::runtime(block);
            return elle::make_unique<doughnut::User>
              (ub->key(), ub->name());
          }
          catch (MissingBlock const&)
          {
            ELLE_TRACE("Reverse UB not found, returning no name");
            return elle::make_unique<doughnut::User>(pub, "");
          }
        }
        else
        {
          ELLE_TRACE_SCOPE("%s: fetch user from name", *this);
          try
          {
            auto block = this->fetch(UB::hash_address(data.string()));
            auto ub = elle::cast<UB>::runtime(block);
            return elle::make_unique<doughnut::User>
              (ub->key(), data.string());
          }
          catch (infinit::model::MissingBlock const&)
          {
            return nullptr;
          }
        }
      }

      void
      Doughnut::_store(blocks::Block& block, StoreMode mode)
      {
        this->_consensus->store(*this->_overlay, block, mode);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        std::unique_ptr<blocks::Block> res;
        try
        {
          return this->_consensus->fetch(*this->_overlay, address);
        }
        catch (infinit::storage::MissingKey const&)
        {
          return nullptr;
        }
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_consensus->remove(*this->_overlay, address);
      }

      DoughnutModelConfig::~DoughnutModelConfig()
      {}

      DoughnutModelConfig::DoughnutModelConfig(
        std::unique_ptr<overlay::Configuration> overlay_,
        cryptography::rsa::KeyPair keys_,
        cryptography::rsa::PublicKey owner_,
        Passport passport_,
        boost::optional<std::string> name_)
        : overlay(std::move(overlay_))
        , keys(std::move(keys_))
        , owner(std::move(owner_))
        , passport(std::move(passport_))
        , name(std::move(name_))
      {}

      DoughnutModelConfig::DoughnutModelConfig
        (elle::serialization::SerializerIn& s)
        : overlay(s.deserialize<std::unique_ptr<overlay::Configuration>>
                  ("overlay"))
        , keys(s.deserialize<cryptography::rsa::KeyPair>("keys"))
        , owner(s.deserialize<cryptography::rsa::PublicKey>("owner"))
        , passport(s.deserialize<Passport>("passport"))
        , name(s.deserialize<boost::optional<std::string>>("name"))
      {}

      void
      DoughnutModelConfig::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("overlay", this->overlay);
        s.serialize("keys", this->keys);
        s.serialize("owner", this->owner);
        s.serialize("passport", this->passport);
        s.serialize("name", this->name);
      }

      std::unique_ptr<infinit::model::Model>
      DoughnutModelConfig::make(std::vector<std::string> const& hosts,
                                bool client,
                                bool server)
      {
        if (!client || !this->name)
          return elle::make_unique<infinit::model::doughnut::Doughnut>(
            keys,
            owner,
            passport,
            overlay->make(hosts, server),
            nullptr);
        else
          return elle::make_unique<infinit::model::doughnut::Doughnut>(
            this->name.get(),
            keys,
            owner,
            passport,
            overlay->make(hosts, server),
            nullptr);
      }

      std::shared_ptr<Doughnut>
      DoughnutModelConfig::make(std::vector<std::string> const& hosts,
                                bool client,
                                std::shared_ptr<Local> local)
      {
        // Doughnut creates a shared pointer to himself in his ctor if passed a
        // Local.
        infinit::model::doughnut::Doughnut* dht = nullptr;
        if (!client || !this->name)
          dht = new infinit::model::doughnut::Doughnut(
            keys,
            owner,
            passport,
            overlay->make(hosts, bool(local)),
            local,
            nullptr);
        else
          dht = new infinit::model::doughnut::Doughnut(
            this->name.get(),
            keys,
            owner,
            passport,
            overlay->make(hosts, bool(local)),
            local,
            nullptr);
        if (local)
          return local->doughnut();
        else
          return std::shared_ptr<Doughnut>(dht);
      }

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<DoughnutModelConfig> _register_DoughnutModelConfig("doughnut");
    }
  }
}
