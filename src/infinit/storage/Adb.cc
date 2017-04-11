#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <elle/system/Process.hh>
#include <infinit/storage/Adb.hh>

static const char* tmpIn = "/tmp/adb_file_in";
static const char* tmpOut = "/tmp/adb_file_out";

namespace infinit
{
  namespace storage
  {
    Adb::Adb(std::string const& root)
    :_root(root)
    {}

    namespace
    {
      std::string remote(std::string const& root,
                         Key const& key)
      {
        return elle::sprintf("%s/%x", root, key);
      }

      void
      system(std::initializer_list<std::string> args)
      {
        elle::system::Process p(args);
        p.wait();
      }
    }

    elle::Buffer Adb::_get(Key key) const
    {
      system({"adb", "pull", remote(_root, key), tmpIn});
      struct stat st;
      ::stat(tmpIn, &st);
      std::ifstream ifs(tmpIn, std::ios::binary);
      elle::Buffer res;
      res.size(st.st_size);
      ifs.read((char*)res.mutable_contents(), st.st_size);
      unlink(tmpIn);
      return res;
    }

    int
    Adb::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      std::ofstream ofs(tmpOut);
      ofs.write((char*)value.contents(), value.size());
      ofs.close();
      system({"adb", "push", tmpOut, remote(_root, key)});
      unlink(tmpOut);
      return 0;
    }

    int
    Adb::_erase(Key key)
    {
      system({"adb", "shell", "rm", remote(_root, key)});
      return 0;
    }

    std::vector<Key> Adb::_list()
    {
      system({
        "sh",
        "-c",
        "adb shell ls " + _root + " > " + tmpOut
      });
      std::ifstream ifs(tmpOut);
      std::vector<Key> res;
      while (true)
      {
        std::string s;
        std::getline(ifs, s);
        if (s.empty())
          break;
        if (is_block(s))
          res.emplace_back(Key::from_string(s.substr(2)));
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
