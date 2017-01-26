#pragma once

#include <string>

#include <boost/optional.hpp>

namespace infinit
{
  namespace cli
  {
    void
    setxattr(std::string const& path,
             std::string const& attribute,
             std::string const& value,
             bool fallback);

    int
    getxattr(std::string const& file,
             std::string const& key,
             char* val,
             int val_size,
             bool fallback_xattrs);

    boost::optional<std::string>
    path_mountpoint(std::string const& path, bool fallback);

    void
    enforce_in_mountpoint(std::string const& path, bool fallback);
  }
}
