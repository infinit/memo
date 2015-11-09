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
#include <infinit/model/doughnut/Consensus.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Replicator.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

#include <infinit/model/doughnut/CHB.cc>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Doughnut::Doughnut(Address id,
                         cryptography::rsa::KeyPair keys,
                         cryptography::rsa::PublicKey owner,
                         Passport passport,
                         ConsensusBuilder consensus,
                         OverlayBuilder overlay_builder,
                         boost::optional<int> port,
                         std::unique_ptr<storage::Storage> storage)
        : _id(std::move(id))
        , _keys(std::move(keys))
        , _owner(std::move(owner))
        , _passport(std::move(passport))
        , _consensus(consensus(*this))
          // FIXME: initialize overlay with bool(this->_local) instead when the
          // id is in doughnut
        , _overlay(overlay_builder(*this, id, bool(storage)))
        , _local(nullptr)
      {
        if (storage)
          this->_local =
            this->_consensus->make_local(std::move(port), std::move(storage));
        if (this->_local)
        {
          this->overlay()->register_local(this->_local);
          this->_local->serve();
        }
      }

      Doughnut::Doughnut(Address id,
                         std::string const& name,
                         cryptography::rsa::KeyPair keys,
                         cryptography::rsa::PublicKey owner,
                         Passport passport,
                         ConsensusBuilder consensus,
                         OverlayBuilder overlay_builder,
                         boost::optional<int> port,
                         std::unique_ptr<storage::Storage> storage)
        : Doughnut(std::move(id),
                   std::move(keys),
                   std::move(owner),
                   std::move(passport),
                   std::move(consensus),
                   std::move(overlay_builder),
                   std::move(port),
                   std::move(storage))
      {
        auto check_user_blocks = [name, this]
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
                  elle::sprintf(
                    "user block exists at %s(%x) with different key",
                    name, UB::hash_address(name)));
            }
            catch (MissingBlock const&)
            {
              auto user = elle::make_unique<UB>(name, this->keys().K());
              ELLE_TRACE_SCOPE("%s: store user block at %x for %s",
                               *this, user->address(), name);

              this->store(std::move(user));
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
                    "user reverse block exists at %s(%x) "
                    "with different name: %s",
                    name, UB::hash_address(this->keys().K()), ub->name()));
            }
            catch(MissingBlock const&)
            {
              auto user = elle::make_unique<UB>(name, this->keys().K(), true);
              ELLE_TRACE_SCOPE("%s: store reverse user block at %x", *this,
                               user->address());
              this->store(std::move(user));
            }
          };
        _user_init.reset(new reactor::Thread(
                           elle::sprintf("%s: user blocks checker", *this),
                           check_user_blocks));
      }

      Doughnut::~Doughnut()
      {
        if (_user_init)
          _user_init->terminate_now();
        ELLE_TRACE("~Doughnut");
      }

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
      Doughnut::_store(std::unique_ptr<blocks::Block> block,
                       StoreMode mode,
                       std::unique_ptr<ConflictResolver> resolver)
      {
        this->_consensus->store(*this->_overlay,
                                std::move(block),
                                mode,
                                std::move(resolver));
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

      Configuration::~Configuration()
      {}

      Configuration::Configuration(
        std::unique_ptr<consensus::Configuration> consensus_,
        std::unique_ptr<overlay::Configuration> overlay_,
        std::unique_ptr<storage::StorageConfig> storage,
        cryptography::rsa::KeyPair keys_,
        cryptography::rsa::PublicKey owner_,
        Passport passport_,
        boost::optional<std::string> name_,
        boost::optional<int> port_)
        : ModelConfig(std::move(storage))
        , consensus(std::move(consensus_))
        , overlay(std::move(overlay_))
        , keys(std::move(keys_))
        , owner(std::move(owner_))
        , passport(std::move(passport_))
        , name(std::move(name_))
        , port(std::move(port_))
      {}

      Configuration::Configuration(elle::serialization::SerializerIn& s)
        : ModelConfig(s)
        , id(s.deserialize<Address>("id"))
        , consensus(s.deserialize<std::unique_ptr<consensus::Configuration>>(
                      "consensus"))
        , overlay(s.deserialize<std::unique_ptr<overlay::Configuration>>(
                    "overlay"))
        , keys(s.deserialize<cryptography::rsa::KeyPair>("keys"))
        , owner(s.deserialize<cryptography::rsa::PublicKey>("owner"))
        , passport(s.deserialize<Passport>("passport"))
        , name(s.deserialize<boost::optional<std::string>>("name"))
        , port(s.deserialize<boost::optional<int>>("port"))
      {}

      void
      Configuration::serialize(elle::serialization::Serializer& s)
      {
        ModelConfig::serialize(s);
        s.serialize("id", this->id);
        s.serialize("consensus", this->consensus);
        s.serialize("overlay", this->overlay);
        s.serialize("keys", this->keys);
        s.serialize("owner", this->owner);
        s.serialize("passport", this->passport);
        s.serialize("name", this->name);
        s.serialize("port", this->port);
      }

      std::unique_ptr<infinit::model::Model>
      Configuration::make(overlay::NodeEndpoints const& hosts,
                          bool client,
                          boost::filesystem::path const& dir)
      {
        return this->make(hosts, client, dir, false, false);
      }

      std::unique_ptr<Doughnut>
      Configuration::make(overlay::NodeEndpoints const& hosts,
                          bool client,
                          boost::filesystem::path const& dir,
                          bool async,
                          bool cache)
      {
        Doughnut::ConsensusBuilder consensus =
          [&] (Doughnut& dht)
          {
            auto consensus = this->consensus->make(dht);
            if (async)
              consensus = elle::make_unique<consensus::Async>(
                dht, std::move(consensus), dir / "async");
            if (cache)
              consensus = elle::make_unique<consensus::Cache>(
                dht, std::move(consensus), std::chrono::seconds(5));
            return std::move(consensus);
          };
        Doughnut::OverlayBuilder overlay =
          [&] (Doughnut& dht, Address id, bool server)
          {
            return this->overlay->make(std::move(id), hosts, server, &dht);
          };
        auto port = this->port ? this->port.get() : 0;
        std::unique_ptr<storage::Storage> storage;
        if (this->storage)
          storage = this->storage->make();
        std::unique_ptr<Doughnut> dht;
        if (!client || !this->name)
          dht = elle::make_unique<infinit::model::doughnut::Doughnut>(
            this->id,
            keys,
            owner,
            passport,
            std::move(consensus),
            std::move(overlay),
            std::move(port),
            std::move(storage));
        else
          dht = elle::make_unique<infinit::model::doughnut::Doughnut>(
            this->id,
            this->name.get(),
            keys,
            owner,
            passport,
            std::move(consensus),
            std::move(overlay),
            std::move(port),
            std::move(storage));
        return dht;
      }

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<Configuration> _register_Configuration("doughnut");
    }
  }
}
