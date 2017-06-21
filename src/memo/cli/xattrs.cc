#include <memo/cli/xattrs.hh>

#include <memo/cli/xattrs.hh>
#if defined INFINIT_LINUX
# include <attr/xattr.h>
#elif defined INFINIT_MACOSX
# include <sys/xattr.h>
#endif

#include <elle/assert.hh>
#include <elle/err.hh>

namespace memo
{
  namespace cli
  {
    namespace
    {
      bfs::path
      file_xattrs_dir(std::string const& file)
      {
        auto p = bfs::path{file};
        auto filename = p.filename();
        auto dir = p.parent_path();
        // dir might be outside the filesystem, so dont go below file if its a
        // directory
        if (bfs::is_directory(file))
          return p / "$xattrs..";
        else
          return dir / ("$xattrs." + filename.string());
      }
    }

    void
    set_xattr(std::string const& file,
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
      bfs::ofstream ofs(path);
      if (!ofs.good())
        elle::err("unable to open %s for writing", path);
      ofs.write(value.data(), value.size());
      if (ofs.fail())
        elle::err("unable to write attribute value in %s", path);
    }

    int
    get_xattr(std::string const& file,
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
      bfs::ifstream ifs(path);
      if (!ifs.good())
        elle::err("unable to open %s for reading", path);
      ifs.read(val, val_size);
      if (ifs.fail())
        elle::err("unable to read attribute value in %s", path);
      return ifs.gcount();
    }

    boost::optional<std::string>
    path_mountpoint(std::string const& path, bool fallback)
    {
      char buffer[4095];
      int sz = get_xattr(path, "infinit.mountpoint", buffer, sizeof buffer, fallback);
      if (0 < sz)
        return std::string{buffer, size_t(sz)};
      else
        return {};
    }

    void
    enforce_in_mountpoint(std::string const& path_, bool fallback)
    {
      auto path = bfs::absolute(path_);
      if (!bfs::exists(path))
        elle::err("path does not exist: %s", path_);
      for (auto const& p: {path, path.parent_path()})
      {
        auto mountpoint = path_mountpoint(p.string(), fallback);
        if (mountpoint && !mountpoint.get().empty())
          return;
      }
      elle::err("%s not in an Memo volume", path_);
    }

    bool
    path_is_root(std::string const& path, bool fallback)
    {
      char buffer[4095];
      int sz = get_xattr(path, "infinit.root", buffer, sizeof buffer, fallback);
      return 0 <= sz && std::string(buffer, sz) == "true";
    }

    bfs::path
    mountpoint_root(std::string const& path_in_mount, bool fallback)
    {
      enforce_in_mountpoint(path_in_mount, fallback);
      auto res = bfs::absolute(path_in_mount);
      while (!path_is_root(res.string(), fallback))
        res = res.parent_path();
      return res;
    }
  }
}
