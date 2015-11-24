#ifndef INFINIT_STORAGE_CACHE_HH
# define INFINIT_STORAGE_CACHE_HH

# include <set>
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
            boost::optional<int> size = {},
            bool use_list = false, // optimize trusting keys from backend._list()
            bool use_status = true // optimize using backend._status()
            );
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      int
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
      void
      _init() const;

      ELLE_ATTRIBUTE_R(std::unique_ptr<Storage>, storage);
      ELLE_ATTRIBUTE_R(boost::optional<int>, size);
      ELLE_ATTRIBUTE_R(bool, use_list);
      ELLE_ATTRIBUTE_R(bool, use_status);
      typedef std::unordered_map<Key, elle::Buffer> Blocks;
      ELLE_ATTRIBUTE(Blocks, blocks, mutable);
      typedef std::set<Key> Keys;
      ELLE_ATTRIBUTE(boost::optional<Keys>, keys, mutable);
    };
  }
}

#endif
