#ifndef INFINIT_STORAGE_DROPBOX_HH
# define INFINIT_STORAGE_DROPBOX_HH

# include <dropbox/Dropbox.hh>

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

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
      ~Dropbox();

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
      virtual
      BlockStatus
      _status(Key k) override;
      ELLE_ATTRIBUTE(dropbox::Dropbox, dropbox);
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
                           int capacity = 0);
      DropboxStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;

      std::string token;
      boost::optional<std::string> root;
    };

  }
}

#endif
