#include <elle/log.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit.model.Model");

static const elle::Version default_version(
  INFINIT_MAJOR, INFINIT_MINOR, 0);

namespace infinit
{
  namespace model
  {
    Model::Model(boost::optional<elle::Version> version)
      : _version(version ? *version : default_version)
    {
      ELLE_LOG("%s: compatibility version %s", *this, this->_version);
      static elle::Version const current_version = elle::Version(
        INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);
      if (this->_version > current_version)
        throw elle::Error(
          elle::sprintf(
            "compatibility version %s is too recent for infinit version %s",
            this->_version, current_version));
    }

    template <>
    std::unique_ptr<blocks::MutableBlock>
    Model::make_block(elle::Buffer data, Address addr) const
    {
      auto res = this->_make_mutable_block();
      res->data(std::move(data));
      return res;
    }

    std::unique_ptr<blocks::MutableBlock>
    Model::_make_mutable_block() const
    {
      ELLE_TRACE_SCOPE("%s: create block", *this);
      return this->_construct_block<blocks::MutableBlock>(
        Address::random(flags::mutable_block));
    }

    template <>
    std::unique_ptr<blocks::ImmutableBlock>
    Model::make_block(elle::Buffer data, Address owner) const
    {
      return this->_make_immutable_block(std::move(data), owner);
    }

    std::unique_ptr<blocks::ImmutableBlock>
    Model::_make_immutable_block(elle::Buffer data, Address owner) const
    {
      ELLE_TRACE_SCOPE("%s: create block", *this);
      return this->_construct_block<blocks::ImmutableBlock>(
        Address::random(flags::immutable_block), std::move(data));
    }

    template <>
    std::unique_ptr<blocks::ACLBlock>
    Model::make_block(elle::Buffer data, Address) const
    {
      auto res = this->_make_acl_block();
      res->data(std::move(data));
      return res;
    }

    template <>
    std::unique_ptr<blocks::GroupBlock>
    Model::make_block(elle::Buffer data, Address) const
    {
      auto res = this->_make_group_block();
      return res;
    }

    std::unique_ptr<blocks::ACLBlock>
    Model::_make_acl_block() const
    {
      ELLE_TRACE_SCOPE("%s: create ACL block", *this);
      return this->_construct_block<blocks::ACLBlock>(
        Address::random(flags::mutable_block));
    }

    std::unique_ptr<blocks::GroupBlock>
    Model::_make_group_block() const
    {
      return this->_construct_block<blocks::GroupBlock>(
        Address::random(flags::mutable_block));
    }

    std::unique_ptr<User>
    Model::make_user(elle::Buffer const& data) const
    {
      ELLE_TRACE_SCOPE("%s: load user from %f", *this, data);
      return this->_make_user(data);
    }

    std::unique_ptr<User>
    Model::_make_user(elle::Buffer const&) const
    {
      return elle::make_unique<User>(); // FIXME
    }

    void
    Model::store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store %f", *this, *block);
      if (!resolver)
      {
        ELLE_WARN("%s: store() called without resolver from %s",
                  this, elle::Backtrace::current());
      }
      block->seal();
      return this->_store(std::move(block), mode, std::move(resolver));
    }

    void
    Model::store(blocks::Block& block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store %f", *this, block);
      if (!resolver)
      {
        ELLE_WARN("%s: store() called without resolver from %s",
                  this, elle::Backtrace::current());
      }
      block.seal();
      auto copy = block.clone();
      return this->_store(std::move(copy), mode, std::move(resolver));
    }

    std::unique_ptr<blocks::Block>
    Model::fetch(Address address, boost::optional<int> local_version) const
    {
      if (auto res = this->_fetch(address, local_version))
      {
        auto val = res->validate(*this);
        if (!val)
        {
          ELLE_WARN("%s: invalid block received for %s:%s", *this, address,
                    val.reason());
          throw elle::Error("invalid block: " + val.reason());
        }
        return res;
      }
      else
      {
        ELLE_ASSERT(local_version);
        return nullptr;
      }
    }

    void
    Model::remove(Address address)
    {
      ELLE_TRACE_SCOPE("%s: remove %f", this, address);
      auto block = this->fetch(address);
      auto rs = block->sign_remove(*this);
      this->remove(address, std::move(rs));
    }

    void
    Model::remove(Address address, blocks::RemoveSignature rs)
    {
      this->_remove(address, std::move(rs));
    }

    ModelConfig::ModelConfig(std::unique_ptr<storage::StorageConfig> storage_,
                             elle::Version version_)
      : storage(std::move(storage_))
      , version(std::move(version_))
    {}

    ModelConfig::ModelConfig(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    ModelConfig::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("storage", this->storage);
      try
      {
        s.serialize("version", this->version);
      }
      catch (elle::Error)
      {
        // Oldest versions did not specify compatibility version.
        this->version = elle::Version(0, 3, 0);
      }
    }
    class DummyConflictResolver: public ConflictResolver
    {
    public:
      DummyConflictResolver() {}
      DummyConflictResolver(elle::serialization::SerializerIn& s) {}
      void serialize(elle::serialization::Serializer& s,
                     elle::Version const&) override
      {
      }
      std::unique_ptr<blocks::Block>
      operator() (blocks::Block& block,
                  blocks::Block& current,
                  model::StoreMode mode) override
      {
        ELLE_WARN("Conflict editing %f, dropping changes", block.address());
        return current.clone();
      }
    };
    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<DummyConflictResolver> _register_dcr("dummy");

    std::unique_ptr<ConflictResolver>
    make_drop_conflict_resolver()
    {
      return elle::make_unique<DummyConflictResolver>();
    }
  }
}
