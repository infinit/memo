#ifndef INFINIT_MODEL_MODEL_HH
# define INFINIT_MODEL_MODEL_HH

# include <memory>

# include <infinit/model/Address.hh>
# include <infinit/model/blocks/fwd.hh>

namespace infinit
{
  namespace model
  {
    class Model
    {
    public:
      Model();
      template <typename Block>
      std::unique_ptr<Block>
      make_block(elle::Buffer data = {}) const;
      void
      store(blocks::Block& block);
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
      void
      _store(blocks::Block& block) = 0;
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
