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
      ELLE_TRACE("%s: compatibility version %s", *this, this->_version);
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
      return this->_construct_block<blocks::MutableBlock>(Address::random());
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
      return this->_construct_block<blocks::ImmutableBlock>(Address::random(),
                                                            std::move(data));
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
      return this->_construct_block<blocks::ACLBlock>(Address::random());
    }

    std::unique_ptr<blocks::GroupBlock>
    Model::_make_group_block() const
    {
      return this->_construct_block<blocks::GroupBlock>(Address::random());
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
      block->seal();
      return this->_store(std::move(block), mode, std::move(resolver));
    }

    void
    Model::store(blocks::Block& block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store %f", *this, block);
      block.seal();
      auto copy = block.clone();
      return this->_store(std::move(copy), mode, std::move(resolver));
    }

    std::unique_ptr<blocks::Block>
    Model::fetch(Address address, boost::optional<int> local_version) const
    {
      if (auto res = this->_fetch(address, local_version))
      {
        if (!res->validate())
        {
          ELLE_WARN("%s: invalid block received for %s", *this, address);
          throw elle::Error("invalid block");
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
      auto block = this->fetch(address);
      auto rs = block->sign_remove();
      this->remove(address, std::move(rs));
    }

    void
    Model::remove(Address address, blocks::RemoveSignature rs)
    {
      this->_remove(address, std::move(rs));
    }

    ModelConfig::ModelConfig(std::unique_ptr<storage::StorageConfig> storage_,
                             boost::optional<elle::Version> version_)
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
      s.serialize("version", this->version);
    }
  }
}
