#pragma once

#include <set>

#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    /// Silo wrapper that keeps blocks in memory.
    class Cache
      : public Storage
    {
    public:
      Cache(std::unique_ptr<Storage> backend,
            boost::optional<int> size = {},
            bool use_list = false, // optimize trusting keys from backend._list()
            bool use_status = true // optimize using backend._status()
            );
      std::string
      type() const override { return "cache"; }

    private:
      /// Update our metrics, based on those of our backend.
      void
      _update_metrics();
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      /// Compute _keys if not already initialized.
      void
      _init() const;

      ELLE_ATTRIBUTE_R(std::unique_ptr<Storage>, storage);
      ELLE_ATTRIBUTE_R(boost::optional<int>, size);
      /// Whether to maintain _keys.
      ELLE_ATTRIBUTE_R(bool, use_list);
      ELLE_ATTRIBUTE_R(bool, use_status);
      using Blocks = std::unordered_map<Key, elle::Buffer>;
      /// The cache itself, loaded on demand.
      ELLE_ATTRIBUTE(Blocks, blocks, mutable);
      using Keys = std::set<Key>;
      /// The set of keys of our backend, if _keys is true.  Either
      /// not initialized, or complete: contrary to _blocks which
      /// contains only the blocks we needed, it contains _all_ the
      /// keys from our silo.
      ELLE_ATTRIBUTE(boost::optional<Keys>, keys, mutable);
    };
  }
}
