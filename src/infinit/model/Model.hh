#ifndef INFINIT_MODEL_MODEL_HH
# define INFINIT_MODEL_MODEL_HH

# include <memory>

# include <boost/filesystem.hpp>

# include <elle/UUID.hh>
# include <elle/Version.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/User.hh>
# include <infinit/model/blocks/fwd.hh>
# include <infinit/serialization.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace overlay
  {
    typedef std::unordered_map<model::Address, std::vector<std::string>>
      NodeEndpoints;
  }

  namespace model
  {
    enum StoreMode
    {
      STORE_INSERT,
      STORE_UPDATE
    };

    // Called in case of conflict error. Returns the new block to retry with
    // or null to abort
    class ConflictResolver
      : public elle::serialization::VirtuallySerializable<false>
    {
    public:
      typedef infinit::serialization_tag serialization_tag;
      virtual
      std::unique_ptr<blocks::Block>
      operator () (blocks::Block& failed,
                   blocks::Block& current,
                   StoreMode mode) = 0;
      virtual
      void
      serialize(elle::serialization::Serializer& s) override = 0;
    };

    class Model
    {
    public:
      Model(boost::optional<elle::Version> version = {});
      ELLE_ATTRIBUTE_R(elle::Version, version);
      template <typename Block>
      std::unique_ptr<Block>
      make_block(elle::Buffer data = elle::Buffer(), Address owner = Address::null) const;
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
      _make_immutable_block(elle::Buffer content, Address owner = Address::null) const;
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
      _remove(Address address, blocks::RemoveSignature sig) = 0;
    };

    struct ModelConfig:
      public elle::serialization::VirtuallySerializable<false>
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
      make(overlay::NodeEndpoints const& hosts,
           bool client,
           boost::filesystem::path const& dir) = 0;
      typedef infinit::serialization_tag serialization_tag;
    };
  }
}

# include <infinit/model/Model.hxx>

#endif
