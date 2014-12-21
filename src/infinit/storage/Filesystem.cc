#include <infinit/storage/Filesystem.hh>

#include <iterator>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include <elle/factory.hh>
#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Filesystem");

namespace infinit
{
  namespace storage
  {
    Filesystem::Filesystem(boost::filesystem::path root)
      : Storage()
      , _root(std::move(root))
    {}

    elle::Buffer
    Filesystem::_get(Key key) const
    {
      boost::filesystem::ifstream input(this->_path(key));
      if (!input.good())
        throw MissingKey(key);
      elle::Buffer res;
      elle::IOStream output(new elle::OutputStreamBuffer<elle::Buffer>(res));
      std::copy(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>(),
                std::ostreambuf_iterator<char>(output));
      return res;
    }

    void
    Filesystem::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      auto path = this->_path(key);
      bool exists = boost::filesystem::exists(path);
      if (!exists && !insert)
        throw MissingKey(key);
      if (exists && !update)
        throw Collision(key);
      boost::filesystem::ofstream output(path);
      if (!output.good())
        throw elle::Error(
          elle::sprintf("unable to open for writing: %s", path));
      elle::IOStream input(new elle::InputStreamBuffer<elle::Buffer>(value));
      std::copy(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>(),
                std::ostreambuf_iterator<char>(output));
      if (insert && update)
        ELLE_DEBUG("%s: block %s", *this, exists ? "updated" : "inserted");
    }

    void
    Filesystem::_erase(Key key)
    {
      auto path = this->_path(key);
      if (!exists(path))
        throw MissingKey(key);
      remove(path);
    }

    boost::filesystem::path
    Filesystem::_path(Key const& key) const
    {
      return this->root() / elle::sprintf("%x", key);
    }

    static std::unique_ptr<Storage> make(std::vector<std::string> const& args)
    {
      return elle::make_unique<infinit::storage::Filesystem>(args[0]);
    }
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "filesystem", &infinit::storage::make);
