#include <infinit/filesystem/xattribute.hh>
#include <infinit/filesystem/Directory.hh>
#include <sys/stat.h>

#ifdef INFINIT_WINDOWS
#undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.xattr");

namespace infinit
{
  namespace filesystem
  {
    XAttributeFile::XAttributeFile(std::shared_ptr<rfs::Path> file,
                                   std::string const& name)
      : _file(file)
      , _name(name)
      {}
    void
    XAttributeFile::stat(struct stat* st)
    {
      std::string attr;
      try
      {
        attr = _file->getxattr(_name);
      }
      catch (elle::Error const&) {}
      st->st_size = attr.size();
#ifndef INFINIT_WINDOWS
      st->st_blocks = st->st_size / 512;
#endif
      st->st_mode =  S_IFREG;
    }
    void
    XAttributeFile::truncate(off_t new_size)
    {
    }
    void
    XAttributeFile::unlink()
    {
      _file->removexattr(_name);
    }
    std::unique_ptr<rfs::Handle>
    XAttributeFile::open(int flags, mode_t mode)
    {
      return elle::make_unique<XAttributeHandle>(_file, _name);
    }
    std::unique_ptr<rfs::Handle>
    XAttributeFile::create(int flags, mode_t mode)
    {
      return open(flags, mode);
    }

    XAttributeHandle::XAttributeHandle(std::shared_ptr<rfs::Path> file,
                                       std::string const& name)
      : _file(file)
      , _name(name)
      , _written(false)
      {
      }
    int
    XAttributeHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      std::string val = _file->getxattr(_name);
      if (offset >= signed(val.size()))
        return 0;
      size = std::min(uint64_t(size), uint64_t(val.size() - offset));
      memcpy(buffer.mutable_contents(), val.data() + offset, size);
      return size;
    }
    int
    XAttributeHandle::write(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      if (_value.size() < size + offset)
        _value.resize(size + offset);
      memcpy((void*)(_value.data() + offset), buffer.mutable_contents(), size);
      _written = true;
      return size;
    }
    void
    XAttributeHandle::close()
    {
      if (_written && _value != "$donotchange")
        _file->setxattr(_name, _value, 0);
    }

    XAttributeDirectory::XAttributeDirectory(std::shared_ptr<rfs::Path> file)
      : _file(file)
      {}
    void
    XAttributeDirectory::list_directory(rfs::OnDirectoryEntry cb)
    {
      auto attrs = _file->listxattr();
      attrs.push_back("user.infinit.block");
      attrs.push_back("user.infinit.auth");
      attrs.push_back("user.infinit.auth.inherit");
      for (auto const& a: attrs)
      {
        std::string value;
        try {value = _file->getxattr(a);} catch(...) {}
        struct stat st;
        st.st_size = value.size();
#ifndef INFINIT_WINDOWS
        st.st_blocks = st.st_size / 512;
#endif
        st.st_mode =  S_IFREG;
        ELLE_TRACE("xad announcing %s", a);
        cb(a, &st);
      }
    }
    std::shared_ptr<rfs::Path>
    XAttributeDirectory::child(std::string const& name)
    {
      ELLE_TRACE("xad queryed for %s", name);
      return std::make_shared<XAttributeFile>(_file, name);
    }
    void
    XAttributeDirectory::stat(struct stat* st)
    {
      st->st_mode = DIRECTORY_MASK;
      st->st_size = 0;
    }
  }
}