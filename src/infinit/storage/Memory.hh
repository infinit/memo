#pragma once

#include <infinit/storage/Key.hh>
#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    /// In-memory storage.
    class Memory
      : public Storage
    {
    public:
      using Blocks = std::unordered_map<Key, elle::Buffer>;

      Memory();
      /// The blocks will not be copied nor released, they must remain
      /// alive at least as long as `this`.
      Memory(Blocks& blocks);
      ~Memory();
      /// Number of bytes stored.
      std::size_t
      size() const;
      std::string
      type() const override { return "memory"; }

    protected:
      /// Retrieve a key, or throw if missing.
      typename Blocks::iterator _find(Key k);
      /// Retrieve a key, or throw if missing.
      typename Blocks::const_iterator _find(Key k) const;
      /// Check all is well.
      void
      _check_invariants() const;
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      /// The blocks, with their deleter.
      ELLE_ATTRIBUTE((std::unique_ptr<Blocks, std::function<void (Blocks*)>>),
                     blocks);
    };

    struct MemoryStorageConfig
      : public StorageConfig
    {
      using Super = StorageConfig;
      using Super::Super;
      void
      serialize(elle::serialization::Serializer& s) override;

      std::unique_ptr<infinit::storage::Storage>
      make() override;
    };
  }
}
