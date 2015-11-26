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
    public:
      typedef std::unordered_map<Key, elle::Buffer> Blocks;
      Memory();
      Memory(Blocks& blocks);

    protected:
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
      ELLE_ATTRIBUTE((std::unique_ptr<Blocks, std::function<void (Blocks*)>>),
                     blocks);
    };

    struct MemoryStorageConfig
      : public StorageConfig
    {
      MemoryStorageConfig(std::string name, int64_t capacity = 0);
      MemoryStorageConfig(elle::serialization::SerializerIn& input);

      void
      serialize(elle::serialization::Serializer& s) override;

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;
    };
  }
}

#endif

