#pragma once

#include <memory>

#include <boost/filesystem.hpp>

#include <elle/UUID.hh>
#include <elle/Version.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/Endpoints.hh>
#include <infinit/model/User.hh>
#include <infinit/model/blocks/fwd.hh>
#include <infinit/serialization.hh>
#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace model
  {
    enum StoreMode
    {
      STORE_INSERT,
      STORE_UPDATE
    };

    enum class Squash
    {
      stop, // Stop searching, do not squash
      skip, // keep searching, do not squash
      at_first_position_stop, // Stop searching, squash at first block
      at_last_position_stop,  // Stop searching, squash at second block
      at_first_position_continue, // Remember candidate, but keep searching
      at_last_position_continue,  // Remember candidate, but keep searching
    };

    struct SquashConflictResolverOptions
    {
      SquashConflictResolverOptions();
      SquashConflictResolverOptions(int max_size);
      int max_size;
    };

    using SquashOperation = std::pair<Squash, SquashConflictResolverOptions>;

    // Called in case of conflict error. Returns the new block to retry with
    // or null to abort
    class ConflictResolver
      : public elle::serialization::VirtuallySerializable<true>
    {
    public:
      typedef infinit::serialization_tag serialization_tag;
      using SquashStack = std::vector<std::unique_ptr<ConflictResolver>>;
      virtual
      std::unique_ptr<blocks::Block>
      operator () (blocks::Block& failed,
                   blocks::Block& current,
                   StoreMode mode) = 0;
      virtual
      SquashOperation
      squashable(SquashStack const& others)
      { return {Squash::stop, {}};}
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& v) override = 0;

      virtual
      std::string
      description() const = 0;

      SquashOperation
      squashable(ConflictResolver& prev);
    };

    std::unique_ptr<ConflictResolver>
    make_merge_conflict_resolver(std::unique_ptr<ConflictResolver> a,
                                 std::unique_ptr<ConflictResolver> b,
                                 SquashConflictResolverOptions const& opts);
    std::vector<std::unique_ptr<ConflictResolver>>&
    get_merge_conflict_resolver_content(ConflictResolver& cr);

    // A resolver that just override the previous version.
    class DummyConflictResolver
      : public ConflictResolver
    {
       using Super = ConflictResolver;
    protected:
      DummyConflictResolver();
    public:
      DummyConflictResolver(elle::serialization::SerializerIn& s,
                            elle::Version const& version);

      std::unique_ptr<blocks::Block>
      operator() (blocks::Block& block,
                  blocks::Block& current,
                  model::StoreMode mode) final;

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& v) override;

      std::string
      description() const override;
    };

    class Model
    {
    public:
      using AddressVersion = std::pair<Address, boost::optional<int>>;
      Model(boost::optional<elle::Version> version = {});
      ELLE_ATTRIBUTE_R(elle::Version, version);
      template <typename Block>
      std::unique_ptr<Block>
      make_block(elle::Buffer data = elle::Buffer(),
                 Address owner = Address::null) const;
      std::unique_ptr<User>
      make_user(elle::Buffer const& data) const;
      void
      store(std::unique_ptr<blocks::Block> block,
            StoreMode mode,
            std::unique_ptr<ConflictResolver> = {});
      void
      store(blocks::Block& block,
            StoreMode mode,
            std::unique_ptr<ConflictResolver> = {});
      /** Fetch block at \param address
       *
       *  Use \param local_version to avoid refetching the block if it did
       *  not change.
       *
       *  \param address Address of the block to fetch.
       *  \param local_version Optional version already owned by the caller.
       *  \return The block at \param address, or null if its version is
       *          still local_version.
       *  \throws MissingBlock if the block does not exist.
       */
      std::unique_ptr<blocks::Block>
      fetch(Address address, boost::optional<int> local_version = {}) const;
      void
      fetch(std::vector<AddressVersion> const& addresses,
            std::function<void(Address, std::unique_ptr<blocks::Block>,
                               std::exception_ptr)> res) const;
      void
      remove(Address address);
      void
      remove(Address address, blocks::RemoveSignature sig);
    protected:
      template <typename Block, typename ... Args>
      static
      std::unique_ptr<Block>
      _construct_block(Args&& ... args);
      virtual
      std::unique_ptr<blocks::MutableBlock>
      _make_mutable_block() const;
      virtual
      std::unique_ptr<blocks::ImmutableBlock>
      _make_immutable_block(elle::Buffer content,
                            Address owner = Address::null) const;
      virtual
      std::unique_ptr<blocks::ACLBlock>
      _make_acl_block() const;
      virtual
      std::unique_ptr<blocks::GroupBlock>
      _make_group_block() const;
      virtual
      std::unique_ptr<User>
      _make_user(elle::Buffer const& data) const;
      virtual
      void
      _store(std::unique_ptr<blocks::Block> block,
             StoreMode mode,
             std::unique_ptr<ConflictResolver> resolver) = 0;
      virtual
      std::unique_ptr<blocks::Block>
      _fetch(Address address, boost::optional<int> local_version) const = 0;
      virtual
      void
      _fetch(std::vector<AddressVersion> const& addresses,
             std::function<void(Address, std::unique_ptr<blocks::Block>,
                               std::exception_ptr)> res) const;
      virtual
      void
      _remove(Address address, blocks::RemoveSignature sig) = 0;
    };

    struct ModelConfig
      : public elle::serialization::VirtuallySerializable<false>
    {
      static constexpr char const* virtually_serializable_key = "type";
      std::unique_ptr<infinit::storage::StorageConfig> storage;
      elle::Version version;
      ModelConfig(std::unique_ptr<storage::StorageConfig> storage,
                  elle::Version version);
      ModelConfig(elle::serialization::SerializerIn& s);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::model::Model>
      make(bool client,
           boost::filesystem::path const& dir) = 0;
      typedef infinit::serialization_tag serialization_tag;
    };
  }
}

#include <infinit/model/Model.hxx>

