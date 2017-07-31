#include <memo/silo/Dropbox.hh>

#include <elle/log.hh>

#include <memo/silo/Collision.hh>
#include <memo/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("memo.silo.Dropbox");

namespace memo
{
  namespace silo
  {
    namespace bfs = boost::filesystem;

    namespace
    {
      std::string
      _key_str(Key key)
      {
        return elle::sprintf("%x", key).substr(2);
      }
    }

    Dropbox::Dropbox(std::string token)
      : Dropbox(std::move(token), ".infinit")
    {}

    Dropbox::Dropbox(std::string token,
                     bfs::path root)
      : _dropbox(std::move(token))
      , _root(std::move(root))
    {}

    bfs::path
    Dropbox::_path(Key key) const
    {
      return this->_root / _key_str(key);
    }

    elle::Buffer
    Dropbox::_get(Key key) const
    {
      ELLE_DEBUG("get %s", _key_str(key));
      try
      {
        ELLE_DEBUG("get path: %s", this->_path(key));
        return this->_dropbox.get(this->_path(key));
      }
      catch (elle::service::dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    int
    Dropbox::_set(Key key,
                  elle::Buffer const& value,
                  bool insert,
                  bool update)
    {
      ELLE_DEBUG("set %s", _key_str(key));
      if (insert)
      {
        auto const insertion =
          this->_dropbox.put(this->_path(key), value, update);
        if (!insertion && !update)
          throw Collision(key);
        return value.size();
      }
      else if (update)
      {
        ELLE_ABORT("not implemented (can dropbox handle it?)");
      }
      else
        elle::err("neither inserting neither updating");
      // FIXME: impl.
      return 0;
    }

    int
    Dropbox::_erase(Key key)
    {
      ELLE_DEBUG("erase %s", _key_str(key));
      try
      {
        this->_dropbox.delete_(this->_path(key));
      }
      catch (elle::service::dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }

      // FIXME: impl.
      return 0;
    }

    std::vector<Key>
    Dropbox::_list()
    {
      try
      {
        auto const metadata = this->_dropbox.metadata("/" + this->_root.string());
        if (!metadata.is_dir)
          elle::err("%s is not a directory", this->_root.string());
        else if (metadata.contents)
          return elle::make_vector(metadata.contents.get(),
                                   [](auto const& entry)
            {
              auto const addr
                = entry.path.substr(entry.path.find_last_of('/') + 1);
              return model::Address::from_string(addr);
            });
        else
          return {};
      }
      catch (elle::service::dropbox::NoSuchFile const& e)
      {
        return {};
      }
    }

    BlockStatus
    Dropbox::_status(Key k)
    {
      auto const p = bfs::path("/" + this->_root.string()) / _key_str(k);
      try
      {
        auto const metadata = this->_dropbox.local_metadata(p);
        ELLE_DEBUG("status check on %x: %s", p, metadata? "exists" : "unknown");
        return metadata ? BlockStatus::exists : BlockStatus::unknown;
      }
      catch (elle::service::dropbox::NoSuchFile const &)
      {
        ELLE_DEBUG("status check on %s: %s", p, "missing");
        return BlockStatus::missing;
      }
    }

    DropboxSiloConfig::DropboxSiloConfig(
      std::string name,
      std::string token,
      boost::optional<std::string> root,
      boost::optional<int64_t> capacity,
      boost::optional<std::string> description)
      : SiloConfig(
          std::move(name), std::move(capacity), std::move(description))
      , token(std::move(token))
      , root(std::move(root))
    {}

    DropboxSiloConfig::DropboxSiloConfig(
      elle::serialization::SerializerIn& s)
      : SiloConfig(s)
      , token(s.deserialize<std::string>("token"))
      , root(s.deserialize<std::string>("root"))
    {}

    void
    DropboxSiloConfig::serialize(elle::serialization::Serializer& s)
    {
      SiloConfig::serialize(s);
      s.serialize("token", this->token);
      s.serialize("root", this->root);
    }

    std::unique_ptr<memo::silo::Silo>
    DropboxSiloConfig::make()
    {
      if (this->root)
        return std::make_unique<memo::silo::Dropbox>(
          this->token, this->root.get());
      else
        return std::make_unique<memo::silo::Dropbox>(this->token);
    }

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<DropboxSiloConfig> _register_DropboxSiloConfig("dropbox");
  }
}
