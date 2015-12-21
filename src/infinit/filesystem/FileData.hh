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


    //Header embeded in each file
    struct FileHeader
    {
      uint64_t size;
      uint64_t links;
      uint32_t mode;
      uint32_t uid;
      uint32_t gid;
      uint64_t atime; // access:  read,
      uint64_t mtime; // content change  dir: create/delete file
      uint64_t ctime; //attribute change+content change
      uint64_t block_size; // size of each data block
      std::unordered_map<std::string, elle::Buffer> xattrs;
      boost::optional<std::string> symlink_target;

      typedef infinit::serialization_tag serialization_tag;

      FileHeader(uint64_t size, uint64_t links, uint32_t mode, uint64_t atime,
        uint64_t mtime, uint64_t ctime, uint64_t block_size,
        std::unordered_map<std::string, elle::Buffer> xattrs
          = std::unordered_map<std::string, elle::Buffer>())
        : size(size)
        , links(links)
        , mode(mode)
        , uid(0)
        , gid(0)
        , atime(atime)
        , mtime(mtime)
        , ctime(ctime)
        , block_size(block_size)
        , xattrs(std::move(xattrs))
      {
        #ifndef INFINIT_WINDOWS
        uid = getuid();
        gid = getgid();
        #endif
      }

      FileHeader()
        : size(0)
        , links(0)
        , mode(0)
        , uid(0)
        , gid(0)
        , atime(0)
        , mtime(0)
        , ctime(0)
        , block_size(0)
      {
        #ifndef INFINIT_WINDOWS
        uid = getuid();
        gid = getgid();
        #endif
      }

      FileHeader(elle::serialization::SerializerIn& s)
      {
        serialize(s);
      }

      void serialize(elle::serialization::Serializer& s)
      {
        s.serialize("size", size);
        s.serialize("links", links);
        s.serialize("mode", mode);
        s.serialize("atime", atime);
        s.serialize("mtime", mtime);
        s.serialize("ctime", ctime);
        s.serialize("block_size", block_size);
        s.serialize("uid", uid);
        s.serialize("gid", gid);
        s.serialize("symlink_target", symlink_target);
        s.serialize("xattrs", xattrs);
      }
    };
  }
}

#endif
