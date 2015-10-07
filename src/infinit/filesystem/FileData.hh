#ifndef INFINIT_FILESYSTEM_FILEDATA_HH
# define INFINIT_FILESYSTEM_FILEDATA_HH

# include <infinit/serialization.hh>
# include <elle/serialization/Serializer.hh>
# include <elle/serialization/json/SerializerIn.hh>
# include <elle/serialization/json/SerializerOut.hh>
# include <elle/log.hh>

namespace infinit
{
  namespace filesystem
  {
    typedef infinit::model::Address Address;

    static const int header_size = sizeof(Address::Value);
    static_assert(sizeof(Address) == header_size, "Glitch in Address size");

    static Address::Value zeros = {0};

    struct FileData
    {
      std::string name;
      uint64_t size;
      uint32_t mode;
      uint32_t uid;
      uint32_t gid;
      uint64_t atime; // access:  read,
      uint64_t mtime; // content change  dir: create/delete file
      uint64_t ctime; //attribute change+content change
      Address address;
      boost::optional<std::string> symlink_target;
      std::unordered_map<std::string, elle::Buffer> xattrs;
      typedef infinit::serialization_tag serialization_tag;

      FileData(std::string name, uint64_t size, uint32_t mode, uint64_t atime,
        uint64_t mtime, uint64_t ctime, Address const& address,
        std::unordered_map<std::string, elle::Buffer> xattrs)
      : name(name)
        , size(size)
        , mode(mode)
        , atime(atime)
        , mtime(mtime)
        , ctime(ctime)
        , address(address)
        , xattrs(std::move(xattrs))
      {}

      FileData()
      : size(0)
        , mode(0)
        , uid(0)
        , gid(0)
        , atime(0)
        , mtime(0)
        , ctime(0)
        , address(zeros)
      {}

      FileData(elle::serialization::SerializerIn& s)
        : address(zeros)
      {
        s.serialize_forward(*this);
      }

      void serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", name);
        s.serialize("size", size);
        s.serialize("mode", mode);
        s.serialize("atime", atime);
        s.serialize("mtime", mtime);
        s.serialize("ctime", ctime);
        s.serialize("address", address);
        try {
          s.serialize("uid", uid);
          s.serialize("gid", gid);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_LOG_COMPONENT("infinit.fs");
          ELLE_WARN("serialization error %s, assuming old format", e);
        }
        s.serialize("symlink_target", symlink_target);
        s.serialize("xattrs", xattrs);
      }
    };
  }
}

#endif
