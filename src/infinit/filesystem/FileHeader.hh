#pragma once

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>
#include <elle/log.hh>

#include <infinit/serialization.hh>

#ifdef INFINIT_WINDOWS
namespace
{
  int getuid()
  {
    return 0;
  }

  int getgid()
  {
    return 0;
  }
}
#endif

namespace infinit
{
  namespace filesystem
  {
    /// Header embeded in each file.
    struct FileHeader
    {
      using Xattrs = std::unordered_map<std::string, elle::Buffer>;

      FileHeader(uint64_t size, uint64_t links, uint32_t mode, uint64_t atime,
                 uint64_t mtime, uint64_t ctime, uint64_t btime, uint64_t block_size,
                 Xattrs xattrs = Xattrs{})
        : size(size)
        , links(links)
        , mode(mode)
        , atime(atime)
        , mtime(mtime)
        , ctime(ctime)
        , btime(btime)
        , block_size(block_size)
        , xattrs(std::move(xattrs))
      {}

      FileHeader() = default;

      FileHeader(elle::serialization::SerializerIn& s,
                 elle::Version const& v)
      {
        serialize(s, v);
      }

      using serialization_tag = infinit::serialization_tag;

      void serialize(elle::serialization::Serializer& s,
                     elle::Version const& v)
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
        if (s.in())
          btime = 0;
        if (v >= elle::Version(0, 7, 0))
          s.serialize("btime", btime);
      }

      uint64_t size = 0;
      uint64_t links = 0;
      uint32_t mode = 0;
      uint32_t uid = getuid();
      uint32_t gid = getgid();
      uint64_t atime = 0; // access:  read,
      uint64_t mtime = 0; // content change  dir: create/delete file
      uint64_t ctime = 0; //attribute change+content change
      uint64_t btime = 0; // birth time
      uint64_t block_size = 0; // size of each data block
      Xattrs xattrs;
      boost::optional<std::string> symlink_target;
    };
  }
}
