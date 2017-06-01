#include <iostream>
#include <fstream>

#include <boost/filesystem.hpp>

#include <elle/system/Process.hh>
#include <infinit/storage/Adb.hh>

namespace bfs = boost::filesystem;

namespace
{
  // FIXME: not even a PID!  Does not respect $TMPDIR, etc.
  auto const tmpIn = bfs::path("/tmp/adb_file_in");
  auto const tmpOut = bfs::path("/tmp/adb_file_out");
}

namespace infinit
{
  namespace storage
  {
    Adb::Adb(std::string const& root)
      :_root(root)
    {}

    namespace
    {
      std::string
      remote(std::string const& root, Key const& key)
      {
        return elle::sprintf("%s/%x", root, key);
      }

      void
      system(std::initializer_list<std::string> args)
      {
        auto&& p = elle::system::Process(args);
        p.wait();
      }

      int
      adb_get_size(std::string const& root, Key const& key)
      {
        // FIXME: this is obviously inefficient, parsing the result of
        // `shell ls` would be better.
        system({"adb", "pull", remote(root, key), tmpIn.string()});
        return file_size(tmpIn);
      }
    }

    elle::Buffer Adb::_get(Key key) const
    {
      system({"adb", "pull", remote(_root, key), tmpIn.string()});
      auto const size = file_size(tmpIn);
      auto&& ifs = bfs::ifstream(tmpIn, std::ios::binary);
      auto res = elle::Buffer{};
      res.size(size);
      ifs.read((char*)res.mutable_contents(), size);
      remove(tmpIn);
      return res;
    }

    int
    Adb::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      // FIXME: check collision, etc.
      auto const size = adb_get_size(_root, key);
      auto&& ofs = bfs::ofstream(tmpOut);
      ofs.write((char*)value.contents(), value.size());
      ofs.close();
      system({"adb", "push", tmpOut.string(), remote(_root, key)});
      remove(tmpOut);
      return value.size() - size;
    }

    int
    Adb::_erase(Key key)
    {
      auto const size = adb_get_size(_root, key);
      system({"adb", "shell", "rm", remote(_root, key)});
      return - size;
    }

    std::vector<Key>
    Adb::_list()
    {
      system({
        "sh",
        "-c",
        "adb shell ls " + _root + " > " + tmpOut.string()
      });
      auto&& ifs = bfs::ifstream(tmpOut);
      auto res = std::vector<Key>{};
      while (true)
      {
        std::string s;
        std::getline(ifs, s);
        if (s.empty())
          break;
        if (is_block(s))
          res.emplace_back(Key::from_string(s));
      }
      return res;
    }

    AdbStorageConfig::AdbStorageConfig(elle::serialization::SerializerIn& s)
      : StorageConfig(s)
      , root(s.deserialize<std::string>("root"))
    {}

    void
    AdbStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("root", this->root);
    }

    std::unique_ptr<infinit::storage::Storage>
    AdbStorageConfig::make()
    {
      return std::make_unique<infinit::storage::Adb>(root);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<AdbStorageConfig>
    _register_AdbStorageConfig("adb");
  }
}
