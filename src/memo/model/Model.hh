#pragma once

#include <memory>

#include <boost/filesystem.hpp>

#include <elle/UUID.hh>
#include <elle/Version.hh>
#include <elle/das/Symbol.hh>
#include <elle/das/named.hh>

#include <memo/model/Address.hh>
#include <memo/model/Endpoints.hh>
#include <memo/model/User.hh>
#include <memo/model/blocks/fwd.hh>
#include <memo/model/blocks/Block.hh>
#include <memo/serialization.hh>
#include <memo/silo/Silo.hh>

namespace memo
{
  namespace model
  {
    ELLE_DAS_SYMBOL(address);
    ELLE_DAS_SYMBOL(block);
    ELLE_DAS_SYMBOL(conflict_resolver);
    ELLE_DAS_SYMBOL(data);
    ELLE_DAS_SYMBOL(decrypt_data);
    ELLE_DAS_SYMBOL(key);
    ELLE_DAS_SYMBOL(local_version);
    ELLE_DAS_SYMBOL(owner);
    ELLE_DAS_SYMBOL(signature);
    ELLE_DAS_SYMBOL(version);

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

    /// Called in case of conflict error.
    ///
    /// @returns the new block to retry with or null to abort.
    class ConflictResolver
      : public elle::serialization::VirtuallySerializable<ConflictResolver, true>
    {
    public:
      using serialization_tag = memo::serialization_tag;
      using SquashStack = std::vector<std::unique_ptr<ConflictResolver>>;
      virtual
      std::unique_ptr<blocks::Block>
      operator () (blocks::Block& failed,
                   blocks::Block& current) = 0;
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

    /// A resolver that just overrides the previous version.
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
                  blocks::Block& current) final;

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& v) override;

      std::string
      description() const override;
    };

    class Model
      : public elle::Printable
    {
    /*-------------.
    | Construction |
    `-------------*/
    public:
      using AddressVersion = std::pair<Address, boost::optional<int>>;
      template <typename ... Args>
      Model(Args&& ... args);
      using Init = decltype(
        elle::das::make_tuple(
          model::version = boost::optional<elle::Version>()));
      Model(Init args);
      ELLE_ATTRIBUTE_R(elle::Version, version);
    private:
      Model(std::tuple<boost::optional<elle::Version>> args);

    /*-------.
    | Blocks |
    `-------*/
    public:
      template <typename Block>
      std::unique_ptr<Block>
      make_block(elle::Buffer data = {},
                 Address owner = {}) const;
      std::unique_ptr<User>
      make_user(elle::Buffer const& data) const;

      /// Construct an immutable block.
      ///
      /// @param data  Payload of the block.
      /// @param owner Optional owning mutable block to restrict deletion.
      elle::das::named::Function<
        std::unique_ptr<blocks::ImmutableBlock>(
          decltype(data)::Formal<elle::Buffer>,
          decltype(owner = Address::null))>
      make_immutable_block;

      /// Construct a mutable block.
      elle::das::named::Function<
        std::unique_ptr<blocks::MutableBlock>(decltype(owner = Address::null))>
      make_mutable_block;

      /// Construct a named block.
      elle::das::named::Function<
        std::unique_ptr<blocks::Block>(decltype(key)::Formal<elle::Buffer>)>
      make_named_block;

      elle::das::named::Function<
        Address(decltype(key)::Formal<elle::Buffer>)>
      named_block_address;

      /// Fetch block at @param address.
      ///
      /// Use @param local_version to avoid refetching the block if it did
      /// not change.
      ///
      /// @param address Address of the block to fetch.
      /// @param local_version Optional version already owned by the caller.
      /// @return The block at \param address, or null if its version is
      ///         still local_version.
      /// @throws MissingBlock if the block does not exist.
      elle::das::named::Function<
        std::unique_ptr<blocks::Block> (
          decltype(address)::Formal<Address>,
          decltype(local_version = boost::optional<int>()),
          decltype(decrypt_data = boost::optional<bool>()))>
      fetch;

      /// Callback when fetching a block.
      ///
      /// @param address   the requested address.
      /// @param block     the block, or nullptr.
      /// @param exception if non null, a exception raised during the fetch.
      using receive_block_t
        = auto(Address address,
               std::unique_ptr<blocks::Block> block,
               std::exception_ptr exception)
        -> void;
      using ReceiveBlock = std::function<receive_block_t>;

      void
      multifetch(std::vector<AddressVersion> const& addresses,
                 ReceiveBlock res) const;

      /// Insert a new block.
      ///
      /// @param block             New block to insert.
      /// @param conflict_resolver Optional automatic conflict resolver.
      elle::das::named::Function<
        void (
          decltype(block)::Formal<std::unique_ptr<blocks::Block>>,
          decltype(conflict_resolver)::Effective<
            std::nullptr_t, std::nullptr_t, std::unique_ptr<ConflictResolver>>)>
      insert;

      /// Insert an immutable block from data.
      ///
      /// @param data  Payload of the block.
      /// @param owner Optional owning mutable block to restrict deletion.
      elle::das::named::Function<
        Address(
          decltype(data)::Formal<elle::Buffer>,
          decltype(owner = Address::null))>
      insert_immutable_block;

      /// Insert a mutable block from data.
      ///
      /// @param data  Payload of the block.
      /// @param owner Optional owning mutable block to restrict deletion.
      elle::das::named::Function<
        Address(
          decltype(data)::Formal<elle::Buffer>,
          decltype(owner = Address::null))>
      insert_mutable_block;

      void
      seal_and_insert(blocks::Block& block,
                      std::unique_ptr<ConflictResolver> = {});
      /// Update an existing block.
      ///
      /// @param block             Block to update.
      /// @param conflict_resolver Optional automatic conflict resolver.
      elle::das::named::Function<
        void (
          decltype(block)::Formal<std::unique_ptr<blocks::Block>>,
          decltype(conflict_resolver)::Effective<
            std::nullptr_t, std::nullptr_t, std::unique_ptr<ConflictResolver>>,
          decltype(decrypt_data = false))>
      update;
      void
      seal_and_update(blocks::Block& block,
                      std::unique_ptr<ConflictResolver> = {});
      /// Remove an existing block.
      elle::das::named::Function<
        void (
          decltype(address)::Formal<Address>,
          decltype(signature = boost::optional<blocks::RemoveSignature>()))>
      remove;

    private:
      std::unique_ptr<blocks::Block>
      _fetch_impl(Address address,
                  boost::optional<int> local_version,
                  boost::optional<bool> decrypt_data
                  ) const;
    protected:
      template <typename Block, typename ... Args>
      static
      std::unique_ptr<Block>
      _construct_block(Args&& ... args);
      virtual
      std::unique_ptr<blocks::MutableBlock>
      _make_mutable_block(Address owner = Address::null) const;
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
      std::unique_ptr<blocks::Block>
      _make_named_block(elle::Buffer const& key) const;
      virtual
      Address
      _named_block_address(elle::Buffer const& key) const;
      virtual
      std::unique_ptr<blocks::Block>
      _fetch(Address address, boost::optional<int> local_version) const = 0;
      virtual
      void
      _fetch(std::vector<AddressVersion> const& addresses,
             ReceiveBlock res) const;
      virtual
      void
      _insert(std::unique_ptr<blocks::Block> block,
              std::unique_ptr<ConflictResolver> resolver) = 0;
      virtual
      void
      _update(std::unique_ptr<blocks::Block> block,
              std::unique_ptr<ConflictResolver> resolver) = 0;
      virtual
      void
      _remove(Address address, blocks::RemoveSignature sig) = 0;

    public:
      void
      print(std::ostream& out) const override;
    };

    struct ModelConfig
      : public elle::serialization::VirtuallySerializable<ModelConfig, false>
    {
      static constexpr char const* virtually_serializable_key = "type";
      std::unique_ptr<memo::silo::SiloConfig> storage;
      elle::Version version;
      ModelConfig(std::unique_ptr<memo::silo::SiloConfig> storage,
                  elle::Version version);
      ModelConfig(elle::serialization::SerializerIn& s);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<memo::model::Model>
      make(bool client,
           boost::filesystem::path const& dir) = 0;
      using serialization_tag = memo::serialization_tag;
    };
  }
}

#include <memo/model/Model.hxx>
