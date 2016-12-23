#include <infinit/cli/xattrs.hh>

#include <xattrs.hh>
#if defined INFINIT_LINUX
# include <attr/xattr.h>
#elif defined INFINIT_MACOSX
# include <sys/xattr.h>
#endif

#include <boost/filesystem.hpp>

#include <elle/assert.hh>
#include <elle/err.hh>

namespace infinit
{
  namespace cli
  {
    static
    boost::filesystem::path
    file_xattrs_dir(std::string const& file)
    {
      boost::filesystem::path p(file);
      auto filename = p.filename();
      auto dir = p.parent_path();
      boost::filesystem::path res;
      // dir might be outside the filesystem, so dont go below file if its a
      // directory
      if (boost::filesystem::is_directory(file))
        res = p / "$xattrs..";
      else
        res = dir / ("$xattrs." + filename.string());
      boost::system::error_code erc;
      return res;
    }

    void
    setxattr(std::string const& file,
             std::string const& key,
             std::string const& value,
             bool fallback)
    {
#ifndef INFINIT_WINDOWS
# ifdef INFINIT_MACOSX
      int res = ::setxattr(
        file.c_str(), key.c_str(), value.data(), value.size(), 0,
        XATTR_NOFOLLOW);
# else
      int res = ::lsetxattr(
        file.c_str(), key.c_str(), value.data(), value.size(), 0);
#endif
      if (res == 0)
        return;
      if (errno != ENOTSUP || !fallback)
        elle::err("unable to set extended attribute: %s", ::strerror(errno));
#endif
      ELLE_ASSERT(fallback);
      auto path = file_xattrs_dir(file) / key;
      boost::filesystem::ofstream ofs(path);
      if (!ofs.good())
        elle::err("unable to open %s for writing", path);
      ofs.write(value.data(), value.size());
      if (ofs.fail())
        elle::err("unable to write attribute value in %s", path);
    }

    int
    getxattr(std::string const& file,
             std::string const& key,
             char* val,
             int val_size,
             bool fallback)
    {
#ifndef INFINIT_WINDOWS
      int res = -1;
# ifdef INFINIT_MACOSX
      res = ::getxattr(file.c_str(), key.c_str(), val, val_size, 0, XATTR_NOFOLLOW);
# else
      res = lgetxattr(file.c_str(), key.c_str(), val, val_size);
# endif
      if (res >= 0)
        return res;
      if (errno != ENOTSUP || !fallback)
        elle::err("unable to get extended attribute: %s", ::strerror(errno));
#endif
      ELLE_ASSERT(fallback);
      auto path = file_xattrs_dir(file) / key;
      boost::filesystem::ifstream ifs(path);
      if (!ifs.good())
        elle::err("unable to open %s for reading", path);
      ifs.read(val, val_size);
      if (ifs.fail())
        elle::err("unable to read attribute value in %s", path);
      return ifs.gcount();
    }
  }
}
