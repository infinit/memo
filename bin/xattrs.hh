#ifdef INFINIT_LINUX
# include <attr/xattr.h>
#elif defined(INFINIT_MACOSX)
# include <sys/xattr.h>
#endif

inline
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
  boost::filesystem::create_directory(res, erc);
  return res;
}

inline
int
port_getxattr(std::string const& file,
              std::string const& key,
              char* val, int val_size,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
  int res = -1;
# ifdef INFINIT_MACOSX
  res = getxattr(file.c_str(), key.c_str(), val, val_size, 0, XATTR_NOFOLLOW);
# else
  res = lgetxattr(file.c_str(), key.c_str(), val, val_size);
# endif

  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ifstream ifs(attr_dir / key);
  ifs.read(val, val_size);
  return ifs.gcount();
}

inline
int
port_setxattr(std::string const& file,
              std::string const& key,
              std::string const& value,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
# ifdef INFINIT_MACOSX
  int res = setxattr(
    file.c_str(), key.c_str(), value.data(), value.size(), 0, XATTR_NOFOLLOW);
# else
  int res = lsetxattr(file.c_str(), key.c_str(), value.data(), value.size(), 0);
#endif
  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ofstream ofs(attr_dir / key);
  ofs.write(value.data(), value.size());
  return 0;
}
