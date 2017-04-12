#pragma once

#include <boost/filesystem/path.hpp>

#include <infinit/storage/Key.hh>
#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Filesystem
      : public Storage
    {
    public:
      Filesystem(boost::filesystem::path root,
                 boost::optional<int64_t> capacity = {});
      std::string
      type() const override { return "filesystem"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
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
                              boost::optional<int64_t> capacity,
                              boost::optional<std::string> description);
      FilesystemStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::storage::Storage>
      make() override;
      std::string path;
    };
  }
}
