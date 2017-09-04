#pragma once

#include <string>
#include <unordered_map>

#include <boost/filesystem.hpp>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// Interactions with Beyond.
  struct Hub
  {
    /// Displayed named -> path.
    using Files = std::unordered_map<std::string, bfs::path>;

    /// Upload these files to the hub's crash route.
    /// @return Whether we sent successfully.
    static bool upload_crash(Files const& fs);

    /// Upload this file to the hub's log report route.
    /// @param user  the user name
    /// @param log   the path to the tar.gz of logs
    /// @return Whether we sent successfully.
    static bool upload_log(std::string const& user,
                           bfs::path const& log);
  };
}
