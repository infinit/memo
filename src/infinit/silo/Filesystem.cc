#include <infinit/silo/Filesystem.hh>

#include <iterator>
#include <cstring>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include <elle/bench.hh>
#include <elle/Duration.hh>
#include <elle/log.hh>

#include <infinit/silo/Collision.hh>
#include <infinit/silo/MissingKey.hh>
#include <infinit/silo/InsufficientSpace.hh>

ELLE_LOG_COMPONENT("infinit.storage.Filesystem");

namespace infinit
{
  namespace silo
  {
    namespace bfs = boost::filesystem;

    Filesystem::Filesystem(bfs::path root,
                           boost::optional<int64_t> capacity)
      : Storage(std::move(capacity))
      , _root(std::move(root))
    {
      bfs::create_directories(this->_root);
      for (auto const& dir: bfs::directory_iterator(this->_root))
        if (is_directory(dir.path()))
          for (auto const& block: bfs::directory_iterator(dir.path()))
          {
            auto const path = block.path();
            auto const size = file_size(path);
            auto const name = path.filename().string();
            auto const addr = infinit::model::Address::from_string(name);
            this->_size_cache[addr] = size;
            this->_usage += size;
            this->_block_count += 1;
            _notify_metrics();
          }
      ELLE_DEBUG("Recovering _usage (%s) and _size_cache (%s)",
                 this->_usage, this->_size_cache.size());
    }

    elle::Buffer
    Filesystem::_get(Key key) const
    {
      auto&& input = bfs::ifstream(this->_path(key), std::ios::binary);
      if (!input.good())
      {
        ELLE_DEBUG("unable to open for reading: %s", this->_path(key));
        throw MissingKey(key);
      }
      static elle::Bench bench("bench.fsstorage.get", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      elle::Buffer res;
      auto&& output = elle::IOStream(res.ostreambuf());
      std::copy(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>(),
                std::ostreambuf_iterator<char>(output));
      ELLE_DUMP("content: %s", res);
      return res;
    }

    int
    Filesystem::_set(Key key, elle::Buffer const& value,
                     bool insert, bool update)
    {
      ELLE_TRACE("set %x", key);
      static elle::Bench bench("bench.fsstorage.set", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      auto const path = this->_path(key);
      bool const exists = bfs::exists(path);
      int const size = exists ? bfs::file_size(path) : 0;
      int delta = value.size() - size;
      if (this->capacity() && this->usage() + delta > this->capacity())
        throw InsufficientSpace(delta, this->usage(), this->capacity().get());
      if (!exists && !insert)
        throw MissingKey(key);
      if (exists && !update)
        throw Collision(key);
      auto&& output = bfs::ofstream(path, std::ios::binary);
      if (!output.good())
        elle::err("unable to open for writing: %s", path);
      output.write(
        reinterpret_cast<const char*>(value.contents()), value.size());
      if (insert && update)
        ELLE_DEBUG("%s: block %s", *this, exists ? "updated" : "inserted");

      this->_size_cache[key] = value.size();
      this->_block_count += update ? 0 : 1;

      return update ? value.size() - size : value.size();
    }

    int
    Filesystem::_erase(Key key)
    {
      ELLE_TRACE("erase %x", key);
      static elle::Bench bench("bench.fsstorage.erase", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      auto const path = this->_path(key);
      if (!exists(path))
        throw MissingKey(key);
      remove(path);
      this->_block_count -= 1;

      int const delta = this->_size_cache[key];
      this->_size_cache.erase(key);
      ELLE_DEBUG("_erase: -delta = %s", -delta);
      return -delta;
    }

    std::vector<Key>
    Filesystem::_list()
    {
      static elle::Bench bench("bench.fsstorage.list", 10000_sec);
      elle::Bench::BenchScope bs(bench);
      auto res = std::vector<Key>{};
      for (auto const& p: bfs::recursive_directory_iterator(this->root()))
        if (is_block(p))
          res.emplace_back(
            Key::from_string(p.path().filename().string()));
      return res;
    }

    bfs::path
    Filesystem::_path(Key const& key) const
    {
      auto dirname = elle::sprintf("%x", elle::ConstWeakBuffer(
        key.value(), 1)).substr(2);
      auto dir = this->root() / dirname;
      if (!bfs::exists(dir))
        bfs::create_directory(dir);
      return dir / elle::sprintf("%x", key);
    }

    FilesystemStorageConfig::FilesystemStorageConfig(
        std::string name,
        std::string path,
        boost::optional<int64_t> capacity,
        boost::optional<std::string> description)
      : StorageConfig(
          std::move(name), std::move(capacity), std::move(description))
      , path(std::move(path))
    {}

    FilesystemStorageConfig::FilesystemStorageConfig(
      elle::serialization::SerializerIn& s)
      : StorageConfig(s)
      , path(s.deserialize<std::string>("path"))
    {}

    void
    FilesystemStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("path", this->path);
    }

    std::unique_ptr<infinit::silo::Storage>
    FilesystemStorageConfig::make()
    {
      return std::make_unique<infinit::silo::Filesystem>(this->path,
                                                             this->capacity);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<FilesystemStorageConfig>
    _register_FilesystemStorageConfig("filesystem");
  }
}
