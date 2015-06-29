#ifndef INFINIT_STORAGE_MEMORY_HH
# define INFINIT_STORAGE_MEMORY_HH

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Memory
      : public Storage
    {
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k);
      virtual
      std::vector<Key>
      _list() override;
      typedef std::unordered_map<Key, elle::Buffer> Blocks;
      ELLE_ATTRIBUTE(Blocks, blocks);
    };
  }
}

#endif
