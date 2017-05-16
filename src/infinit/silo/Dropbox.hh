#pragma once

#include <elle/service/dropbox/Dropbox.hh>

#include <infinit/silo/Key.hh>
#include <infinit/silo/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Dropbox
      : public Storage
    {
    public:
      Dropbox(std::string token);
      Dropbox(std::string token,
              boost::filesystem::path root);
      ~Dropbox() = default;
      std::string
      type() const override { return "dropbox"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      BlockStatus
      _status(Key k) override;
      ELLE_ATTRIBUTE(elle::service::dropbox::Dropbox, dropbox);
      ELLE_ATTRIBUTE_R(boost::filesystem::path, root);

    private:
      boost::filesystem::path
      _path(Key key) const;
    };

    struct DropboxStorageConfig
      : public StorageConfig
    {
      DropboxStorageConfig(std::string name,
                           std::string token,
                           boost::optional<std::string> root,
                           boost::optional<int64_t> capacity,
                           boost::optional<std::string> description);
      DropboxStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::storage::Storage>
      make() override;

      std::string token;
      boost::optional<std::string> root;
    };
  }
}
