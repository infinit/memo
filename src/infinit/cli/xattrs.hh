#pragma once

#include <string>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

namespace infinit
{
  namespace cli
  {
    namespace bfs = boost::filesystem;

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
             bool fallback);

    boost::optional<std::string>
    path_mountpoint(std::string const& path, bool fallback);

    void
    enforce_in_mountpoint(std::string const& path, bool fallback);

    bool
    path_is_root(std::string const& path, bool fallback);

    bfs::path
    mountpoint_root(std::string const& path_in_mount, bool fallback);
  }
}
