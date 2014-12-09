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
      std::unique_ptr<blocks::Block>
      make_block() const;
      void
      store(blocks::Block& block);
      std::unique_ptr<blocks::Block>
      fetch(Address address) const;
      void
      remove(Address address);
    protected:
      virtual
      std::unique_ptr<blocks::Block>
      _make_block() const = 0;
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

#endif
