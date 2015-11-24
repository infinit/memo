#ifndef INFINIT_STORAGE_FILESYSTEM_HH
# define INFINIT_STORAGE_FILESYSTEM_HH

# include <boost/filesystem/path.hpp>

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Filesystem
      : public Storage
    {
    public:
      Filesystem(boost::filesystem::path root);
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
      ELLE_ATTRIBUTE_R(boost::filesystem::path, root);
    private:
      boost::filesystem::path
      _path(Key const& key) const;
    };

    struct FilesystemStorageConfig
      : public StorageConfig
    {
      FilesystemStorageConfig(std::string name,
                              std::string path,
                              int capacity = 0);
      FilesystemStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;
      std::string path;
    };
  }
}

#endif
