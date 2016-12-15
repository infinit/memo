#pragma once

#include <string>

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
  }
}
