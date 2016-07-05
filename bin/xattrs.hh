#ifndef INFINIT_BIN_XATTRS_HH
# define INFINIT_BIN_XATTRS_HH
# include <string>

# ifdef INFINIT_LINUX
#  include <attr/xattr.h>
# elif defined(INFINIT_MACOSX)
#  include <sys/xattr.h>
# endif

# include <boost/filesystem/path.hpp>
# include <boost/optional/optional.hpp>

# include <elle/error.hh>

class InvalidArgument
  : public elle::Error
{
public:
  InvalidArgument(std::string const& error)
    : elle::Error(error)
  {}
};

class PermissionDenied
  : public elle::Error
{
public:
  PermissionDenied(std::string const& error)
    : elle::Error(error)
  {}
};

boost::filesystem::path
file_xattrs_dir(std::string const& file);

int
port_getxattr(std::string const& file,
              std::string const& key,
              char* val, int val_size,
              bool fallback_xattrs);

int
port_setxattr(std::string const& file,
              std::string const& key,
              std::string const& value,
              bool fallback_xattrs);

boost::optional<std::string>
path_mountpoint(std::string const& path, bool fallback);

void
enforce_in_mountpoint(
  std::string const& path, bool enforce_directory, bool fallback);

bool
path_is_root(std::string const& path, bool fallback);

boost::filesystem::path
mountpoint_root(std::string const& path_in_mount, bool fallback);

# include <xattrs.hxx>

#endif
