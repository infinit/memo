#ifndef INFINIT_MODEL_MODEL_HH
# define INFINIT_MODEL_MODEL_HH

# include <memory>
# include <boost/filesystem.hpp>

# include <infinit/model/Address.hh>
# include <infinit/model/User.hh>
# include <infinit/model/blocks/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace model
  {
    enum StoreMode
    {
      STORE_ANY,
      STORE_INSERT,
      STORE_UPDATE
    };

    // Called in case of conflict error. Returns the new block to retry with
    // or null to abort
    typedef std::function<
      std::unique_ptr<blocks::Block> (blocks::Block& block, StoreMode mode)>
      ConflictResolver;

    class Model
    {
    public:
      Model();
      template <typename Block>
      std::unique_ptr<Block>
      make_block(elle::Buffer data = elle::Buffer()) const;
      std::unique_ptr<User>
      make_user(elle::Buffer const& data) const;
      void
      store(blocks::Block& block, StoreMode mode = STORE_ANY, ConflictResolver = {});
      std::unique_ptr<blocks::Block>
      fetch(Address address) const;
      void
      remove(Address address);
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
      _make_immutable_block(elle::Buffer content) const;
      virtual
      std::unique_ptr<blocks::ACLBlock>
      _make_acl_block() const;
      virtual
      std::unique_ptr<User>
      _make_user(elle::Buffer const& data) const;
      virtual
      void
      _store(blocks::Block& block, StoreMode mode, ConflictResolver resolver) = 0;
      virtual
      std::unique_ptr<blocks::Block>
      _fetch(Address address) const = 0;
      virtual
      void
      _remove(Address address) = 0;
    };

    struct ModelConfig:
      public elle::serialization::VirtuallySerializable
    {
      static constexpr char const* virtually_serializable_key = "type";

      virtual
      std::unique_ptr<infinit::model::Model>
      make(std::vector<std::string> const& hosts, bool client, bool server,
           boost::filesystem::path const& dir) = 0;
      typedef infinit::serialization_tag serialization_tag;
    };
  }
}

# include <infinit/model/Model.hxx>

#endif
