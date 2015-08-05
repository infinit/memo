#include <infinit/storage/Dropbox.hh>

#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Dropbox");

namespace infinit
{
  namespace storage
  {
    Dropbox::Dropbox(std::string token)
      : _dropbox(std::move(token))
    {}

    Dropbox::~Dropbox()
    {}

    static
    boost::filesystem::path
    path()
    {
      static boost::filesystem::path const root = ".infinit";
      return root;
    }

    static
    boost::filesystem::path
    path(Key key)
    {
      return path() / elle::sprintf("%x", key);
    }

    elle::Buffer
    Dropbox::_get(Key key) const
    {
      try
      {
        return this->_dropbox.get(path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    void
    Dropbox::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      if (insert)
      {
        auto insertion =
          this->_dropbox.put(path(key), value, update);
        if (!insertion && !update)
          throw Collision(key);
      }
      else if (update)
      {
        ELLE_ABORT("not implemented (can dropbox handle it?)");
      }
      else
        throw elle::Error("neither inserting neither updating");
    }

    void
    Dropbox::_erase(Key key)
    {
      try
      {
        return this->_dropbox.delete_(path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    std::vector<Key>
    Dropbox::_list()
    {
      auto metadata = this->_dropbox.metadata(path());
      std::vector<Key> res;
      if (!metadata.is_dir || !metadata.contents)
        throw elle::Error(".infinit is not a directory");
      for (auto const& entry: metadata.contents.get())
        res.push_back(model::Address::from_string(entry.path));
      return res;
    }

    struct DropboxStorageConfig
      : public StorageConfig
    {
    public:
      DropboxStorageConfig(elle::serialization::SerializerIn& input)
        : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("token", this->token);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Dropbox>(this->token);
      }

      std::string token;
    };
    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<DropboxStorageConfig> _register_DropboxStorageConfig("dropbox");
  }
}
