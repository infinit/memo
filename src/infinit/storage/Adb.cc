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
    elle::Buffer Adb::_get(Key key) const
    {
      std::string remote = elle::sprintf("%s/%x", _root, key);
      std::vector<std::string> args = {
        "adb",
        "pull",
        remote,
        tmpIn
      };
      elle::system::Process p(args);
      p.wait();
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
      std::string remote = elle::sprintf("%s/%x", _root, key);
      std::ofstream ofs(tmpOut);
      ofs.write((char*)value.contents(), value.size());
      ofs.close();
      std::vector<std::string> args = {
        "adb",
        "push",
        tmpOut,
        remote
      };
      elle::system::Process p(args);
      p.wait();
      unlink(tmpOut);

      return 0;
    }

    int
    Adb::_erase(Key key)
    {
      std::string remote = elle::sprintf("%s/%x", _root, key);
      std::vector<std::string> args = {
        "adb",
        "shell",
        "rm",
        remote
      };
      elle::system::Process p(args);
      p.wait();

      return 0;
    }

    std::vector<Key> Adb::_list()
    {
      std::vector<std::string> args = {
        "sh",
        "-c",
        "adb shell ls " + _root + " > " + tmpOut
      };
      elle::system::Process p(args);
      p.wait();
      std::ifstream ifs(tmpOut);
      std::vector<Key> res;
      while (true)
      {
        std::string s;
        std::getline(ifs, s);
        if (s.empty())
          break;
        if (s.substr(0, 2) != "0x" || s.length()!=66)
          continue;
        Key k = Key::from_string(s.substr(2));
        res.push_back(k);
      }
      return res;
    }

    AdbStorageConfig::AdbStorageConfig(elle::serialization::SerializerIn& input)
    : StorageConfig()
    {
      this->serialize(input);
    }

    void
    AdbStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("root", this->root);
    }

    std::unique_ptr<infinit::storage::Storage>
    AdbStorageConfig::make()
    {
      return elle::make_unique<infinit::storage::Adb>(root);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<AdbStorageConfig>
    _register_AdbStorageConfig("adb");
  }
}
