#include <xattrs.hh>

#include <boost/filesystem.hpp>

#include <elle/assert.hh>

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

boost::optional<std::string>
path_mountpoint(std::string const& path, bool fallback)
{
  char buffer[4095];
  int sz = port_getxattr(path, "infinit.mountpoint", buffer, 4095, fallback);
  if (sz <= 0)
    return {};
  return std::string(buffer, sz);
}

void
enforce_in_mountpoint(
  std::string const& path_, bool enforce_directory, bool fallback)
{
  auto path = boost::filesystem::absolute(path_);
  if (!boost::filesystem::exists(path))
    elle::err(elle::sprintf("path does not exist: %s", path_));
  if (enforce_directory && !boost::filesystem::is_directory(path))
    elle::err(elle::sprintf("path must be a directory: %s", path_));
  for (auto const& p: {path, path.parent_path()})
  {
    auto mountpoint = path_mountpoint(p.string(), fallback);
    if (mountpoint && !mountpoint.get().empty())
      return;
  }
  elle::err(elle::sprintf("%s not in an Infinit volume", path_));
}

bool
path_is_root(std::string const& path, bool fallback)
{
  char buffer[4095];
  int sz = port_getxattr(path, "infinit.root", buffer, 4095, fallback);
  if (sz < 0)
    return false;
  return std::string(buffer, sz) == std::string("true");
}

boost::filesystem::path
mountpoint_root(std::string const& path_in_mount, bool fallback)
{
  enforce_in_mountpoint(path_in_mount, false, fallback);
  boost::filesystem::path res = boost::filesystem::absolute(path_in_mount);
  while (!path_is_root(res.string(), fallback))
    res = res.parent_path();
  return res;
}
