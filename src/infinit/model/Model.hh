#ifndef INFINIT_MODEL_MODEL_HH
# define INFINIT_MODEL_MODEL_HH

# include <memory>

# include <infinit/model/Address.hh>
# include <infinit/model/User.hh>
# include <infinit/model/blocks/fwd.hh>

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
      store(blocks::Block& block, StoreMode mode = STORE_ANY);
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
      _store(blocks::Block& block, StoreMode mode) = 0;
      virtual
      std::unique_ptr<blocks::Block>
      _fetch(Address address) const = 0;
      virtual
      void
      _remove(Address address) = 0;
    };
  }
}

# include <infinit/model/Model.hxx>

#endif
