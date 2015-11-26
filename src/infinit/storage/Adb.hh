#ifndef INFINIT_STORAGE_ADB_HH
# define INFINIT_STORAGE_ADB_HH

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Adb
      : public Storage
    {
    public:
      Adb(std::string const& root);
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
      ELLE_ATTRIBUTE(std::string, root);
    };

    struct AdbStorageConfig
      : public StorageConfig
    {
      AdbStorageConfig(std::string name, int64_t capacity = 0);
      AdbStorageConfig(elle::serialization::SerializerIn& input);

      virtual
      void
      serialize(elle::serialization::Serializer& s) override;

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;

      std::string root;
      std::shared_ptr<StorageConfig> storage;
    };
  }
}

#endif
