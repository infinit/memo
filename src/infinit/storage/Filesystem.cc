#include <infinit/storage/Filesystem.hh>

#include <iterator>
#include <cstring>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include <elle/bench.hh>
#include <elle/Duration.hh>
#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>
#include <infinit/storage/InsufficientSpace.hh>

ELLE_LOG_COMPONENT("infinit.storage.Filesystem");

namespace infinit
{
  namespace storage
  {
    Filesystem::Filesystem(boost::filesystem::path root,
                           boost::optional<int64_t> capacity)
      : Storage(std::move(capacity))
      , _root(std::move(root))
    {
      using namespace boost;
      using namespace boost::filesystem;
      create_directories(this->_root);
      auto dirs = make_iterator_range(
        directory_iterator(this->_root), {});
      for (auto const& dir: dirs)
      {
        auto blocks_path = dir.path();
        if (!is_directory(blocks_path))
          continue;
        auto blocks = make_iterator_range(
          directory_iterator(blocks_path), {});
        for (auto const& block: blocks)
        {
          auto path = block.path();
          auto _file_size = file_size(path);
          auto name = path.filename().string();
          auto addr = infinit::model::Address::from_string(name.substr(2));
          this->_size_cache[addr] = _file_size;
          this->_usage += _file_size;
        }
      }
      ELLE_DEBUG("Recovering _usage (%s) and _size_cache (%s)",
                 this->_usage , this->_size_cache.size());
    }

    elle::Buffer
    Filesystem::_get(Key key) const
    {
      boost::filesystem::ifstream input(this->_path(key), std::ios::binary);
      if (!input.good())
      {
        ELLE_DEBUG("unable to open for reading: %s", this->_path(key));
        throw MissingKey(key);
      }
      static elle::Bench bench("bench.fsstorage.get", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      elle::Buffer res;
      elle::IOStream output(res.ostreambuf());
      std::copy(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>(),
                std::ostreambuf_iterator<char>(output));
      ELLE_DUMP("content: %s", res);
      return res;
    }

    int
    Filesystem::_set(
      Key key, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_TRACE("set %x", key);
      static elle::Bench bench("bench.fsstorage.set", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      auto path = this->_path(key);
      bool exists = boost::filesystem::exists(path);
      int size = 0;
      if (exists)
        size = boost::filesystem::file_size(path);
      int delta = value.size() - size;
      if (this->capacity() && this->usage() + delta > this->capacity())
        throw InsufficientSpace(delta, this->usage(), this->capacity().get());
      if (!exists && !insert)
        throw MissingKey(key);
      if (exists && !update)
        throw Collision(key);
      boost::filesystem::ofstream output(path, std::ios::binary);
      if (!output.good())
        throw elle::Error(
          elle::sprintf("unable to open for writing: %s", path));
      output.write(
        reinterpret_cast<const char*>(value.contents()), value.size());
      if (insert && update)
        ELLE_DEBUG("%s: block %s", *this, exists ? "updated" : "inserted");

      _size_cache[key] = value.size();

      return update ? value.size() - size : value.size();
    }

    int
    Filesystem::_erase(Key key)
    {
      ELLE_TRACE("erase %x", key);
      static elle::Bench bench("bench.fsstorage.erase", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      auto path = this->_path(key);
      if (!exists(path))
        throw MissingKey(key);
      remove(path);

      int delta = this->_size_cache[key];
      this->_size_cache.erase(key);
      ELLE_DEBUG("_erase: -delta = %s", -delta);
      return -delta;
    }

    std::vector<Key>
    Filesystem::_list()
    {
      static elle::Bench bench("bench.fsstorage.list", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      std::vector<Key> res;
      boost::filesystem::recursive_directory_iterator it(this->root());
      boost::filesystem::recursive_directory_iterator iend;
      while (it != iend)
      {
        std::string s = it->path().filename().string();
        if (s.substr(0, 2) != "0x" || s.length()!=66)
        {
          ++it;
          continue;
        }
        Key k = Key::from_string(s.substr(2));
        res.push_back(k);
        ++it;
      }
      return res;
    }

    boost::filesystem::path
    Filesystem::_path(Key const& key) const
    {
      auto dirname = elle::sprintf("%x", elle::ConstWeakBuffer(
        key.value(), 1)).substr(2);
      auto dir = this->root() / dirname;
      if (! boost::filesystem::exists(dir))
        boost::filesystem::create_directory(dir);
      return dir / elle::sprintf("%x", key);
    }

    FilesystemStorageConfig::FilesystemStorageConfig(
        std::string name, std::string path_, boost::optional<int64_t> capacity)
      : StorageConfig(std::move(name), std::move(capacity))
      , path(std::move(path_))
    {}

    FilesystemStorageConfig::FilesystemStorageConfig(
      elle::serialization::SerializerIn& input)
      : StorageConfig()
    {
      this->serialize(input);
    }

    void
    FilesystemStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("path", this->path);
    }

    std::unique_ptr<infinit::storage::Storage>
    FilesystemStorageConfig::make()
    {
      return elle::make_unique<infinit::storage::Filesystem>(this->path,
                                                             this->capacity);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<FilesystemStorageConfig>
    _register_FilesystemStorageConfig("filesystem");
  }
}
