#ifndef INFINIT_UTILITY_HH
# define INFINIT_UTILITY_HH

# include <boost/filesystem.hpp>

# include <elle/os/environ.hh>
# include <elle/system/home_directory.hh>
# include <elle/system/username.hh>

namespace infinit
{
  inline
  boost::filesystem::path
  canonical_folder(boost::filesystem::path const& path)
  {
    if (exists(path) && !is_directory(path))
      throw elle::Error(elle::sprintf("not a directory: %s", path));
    create_directories(path);
    boost::system::error_code erc;
    permissions(
      path, boost::filesystem::add_perms | boost::filesystem::owner_write, erc);
    return canonical(path);
  }

  inline
  boost::filesystem::path
  home()
  {
    static auto const infinit_home = elle::os::getenv("INFINIT_HOME", "");
    return infinit_home.empty() ? elle::system::home_directory() : infinit_home;
  }

  inline
  boost::filesystem::path
  _xdg(std::string const& type,
            boost::filesystem::path const& def)
  {
    auto const infinit = elle::os::getenv("INFINIT_" + type, "");
    auto const xdg = elle::os::getenv("XDG_" + type, "");
    auto const dir =
      !infinit.empty() ? infinit :
      !xdg.empty() ? boost::filesystem::path(xdg) / "infinit/filesystem" :
      def;
    return canonical_folder(dir);
  }

  inline
  boost::filesystem::path
  _xdg_home(std::string const& type,
            boost::filesystem::path const& def)
  {
    return _xdg(type + "_HOME", home() / def / "infinit/filesystem");
  }

  inline
  boost::filesystem::path
  xdg_cache_home()
  {
    return _xdg_home("CACHE", ".cache");
  }

  inline
  boost::filesystem::path
  xdg_config_home()
  {
    return _xdg_home("CONFIG", ".config");
  }

  inline
  boost::filesystem::path
  xdg_data_home()
  {
    return _xdg_home("DATA", ".local/share");
  }

  inline
  boost::filesystem::path
  tmpdir()
  {
    static auto const res = elle::os::getenv("TMPDIR", "/tmp");
    return res;
  }

  inline
  boost::filesystem::path
  xdg_runtime_dir()
  {
    return _xdg(
      "RUNTIME_DIR",
      tmpdir() / elle::sprintf("infinit-%s", elle::system::username()));
  }

  inline
  boost::filesystem::path
  xdg_state_home()
  {
    return _xdg_home("STATE", ".local/state");
  }
}

#endif
