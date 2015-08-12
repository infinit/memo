#ifndef INFINIT_STORAGE_CACHE_HH
# define INFINIT_STORAGE_CACHE_HH

# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Cache
      : public Storage
    {
    public:
      Cache(std::unique_ptr<Storage> backend,
            boost::optional<int> size = {});
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;

      ELLE_ATTRIBUTE_R(std::unique_ptr<Storage>, storage);
      ELLE_ATTRIBUTE_R(boost::optional<int>, size);
      typedef std::unordered_map<Key, elle::Buffer> Blocks;
      ELLE_ATTRIBUTE_P(Blocks, blocks, mutable);
      typedef std::vector<Key> Keys;
      ELLE_ATTRIBUTE_P(boost::optional<Keys>, keys, mutable);
    };
  }
}

#endif
