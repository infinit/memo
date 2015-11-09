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
      : Dropbox(std::move(token), ".infinit")
    {}

    Dropbox::Dropbox(std::string token,
                     boost::filesystem::path root)
      : _dropbox(std::move(token))
      , _root(std::move(root))
    {}

    Dropbox::~Dropbox()
    {}

    boost::filesystem::path
    Dropbox::_path(Key key) const
    {
      return this->_root / elle::sprintf("%x", key);
    }

    elle::Buffer
    Dropbox::_get(Key key) const
    {
      ELLE_DEBUG("get %x", key);
      try
      {
        ELLE_DEBUG("get path: %s", this->_path(key));
        return this->_dropbox.get(this->_path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    void
    Dropbox::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_DEBUG("set %x", key);
      if (insert)
      {
        auto insertion =
          this->_dropbox.put(this->_path(key), value, update);
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
      ELLE_DEBUG("erase %x", key);
      try
      {
        return this->_dropbox.delete_(this->_path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    std::vector<Key>
    Dropbox::_list()
    {
      auto metadata = this->_dropbox.metadata("/" + this->_root.string());
      std::vector<Key> res;
      if (!metadata.is_dir)
        throw elle::Error(".infinit is not a directory");
      if  (!metadata.contents)
        return res;
      for (auto const& entry: metadata.contents.get())
      {
        // /.infinit/0xFOO -> FOO
        std::string address = entry.path.substr(entry.path.find_last_of('/')+3);
        res.push_back(model::Address::from_string(address));
      }
      return res;
    }

    BlockStatus
    Dropbox::_status(Key k)
    {
      boost::filesystem::path p("/" + this->_root.string());
      p = p / elle::sprintf("%x", k);
      try
      {
        auto metadata = this->_dropbox.local_metadata(p);
        ELLE_DEBUG("status check on %x: %s", p, metadata? "exists" : "unknown");
        return metadata? BlockStatus::exists : BlockStatus::unknown;
      }
      catch (dropbox::NoSuchFile const &)
      {
        ELLE_DEBUG("status check on %x: %s", p, "missing");
        return BlockStatus::missing;
      }
    }

    DropboxStorageConfig::DropboxStorageConfig(
      std::string name,
      std::string token_,
      boost::optional<std::string> root_)
      : StorageConfig(std::move(name))
      , token(std::move(token_))
      , root(std::move(root_))
    {}

    DropboxStorageConfig::DropboxStorageConfig(
      elle::serialization::SerializerIn& input)
      : StorageConfig()
    {
      this->serialize(input);
    }

    void
    DropboxStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("token", this->token);
      s.serialize("root", this->root);
    }

    std::unique_ptr<infinit::storage::Storage>
    DropboxStorageConfig::make()
    {
      if (this->root)
        return elle::make_unique<infinit::storage::Dropbox>(
          this->token, this->root.get());
      else
        return elle::make_unique<infinit::storage::Dropbox>(this->token);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<DropboxStorageConfig> _register_DropboxStorageConfig("dropbox");
  }
}
