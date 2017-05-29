#pragma once

#include <boost/filesystem.hpp>

#include <elle/Option.hh>
#include <elle/Version.hh>
#include <elle/err.hh>
#include <elle/os/environ.hh>
#include <elle/system/user_paths.hh>
#include <elle/system/username.hh>
#include <elle/unordered_map.hh>

#include <elle/reactor/http/Request.hh>

#include <infinit/User.hh>

namespace infinit
{
  elle::Version
  version();

  std::string
  version_describe();

  std::string
  beyond(bool help = false);

  /// Typically "hub".
  std::string
  beyond_delegate_user();

  class MissingResource
    : public elle::Error
  {
  public:
    template <typename ... Args>
    MissingResource(Args&& ... args)
      : elle::Error(std::forward<Args>(args)...)
    {}
  };

  class MissingLocalResource
    : public MissingResource
  {
  public:
    template <typename ... Args>
    MissingLocalResource(Args&& ... args)
      : MissingResource(std::forward<Args>(args)...)
    {}
  };

  class ResourceGone
    : public MissingResource
  {
  public:
    template <typename ... Args>
    ResourceGone(Args&& ... args)
      : MissingResource(std::forward<Args>(args)...)
    {}
  };

  class ResourceProtected
    : public elle::Error
  {
  public:
    template <typename ... Args>
    ResourceProtected(Args&& ... args)
      : elle::Error(std::forward<Args>(args)...)
    {}
  };

  class ResourceAlreadyFetched
    : public elle::Error
  {
  public:
    template <typename ... Args>
    ResourceAlreadyFetched(Args&& ... args)
      : elle::Error(std::forward<Args>(args)...)
    {}
  };

  class Redirected
    : public elle::Error
  {
  public:
    Redirected(std::string const& url)
      : elle::Error(
        elle::sprintf("%s caused an unsupported redirection", url))
    {}
  };

  inline
  boost::filesystem::path
  canonical_folder(boost::filesystem::path const& path)
  {
    if (exists(path) && !is_directory(path))
      elle::err("not a directory: %s", path);
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
    auto const infinit_home = elle::os::getenv("MEMO_HOME", "");
    return infinit_home.empty() ? elle::system::home_directory() : infinit_home;
  }

  inline
  boost::filesystem::path
  _xdg(std::string const& type,
       boost::filesystem::path const& def)
  {
    auto const infinit = elle::os::getenv("MEMO_" + type, "");
    auto const xdg = elle::os::getenv("XDG_" + type, "");
    auto const dir =
      !infinit.empty() ? infinit :
      !xdg.empty() ? boost::filesystem::path(xdg) / "infinit/memo" :
      def;
    try
    {
      return canonical_folder(dir);
    }
    catch (boost::filesystem::filesystem_error& e)
    {
      ELLE_LOG_COMPONENT("xdg");
      std::string env =
        !infinit.empty() ? "MEMO_" : !xdg.empty() ? "XDG_" : "";
      if (!env.empty())
        ELLE_WARN("Invalid \"%s%s\" directory: %s", env, type, e.what());
      elle::err(e.what());
    }
  }

  inline
  boost::filesystem::path
  _xdg_home(std::string const& type,
            boost::filesystem::path const& def)
  {
    return _xdg(type + "_HOME", home() / def / "infinit/memo");
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
    return elle::os::getenv("TMPDIR", "/tmp");;
  }

  inline
  boost::filesystem::path
  xdg_runtime_dir(boost::optional<std::string> fallback = {})
  {
    return _xdg(
      "RUNTIME_DIR",
      fallback
        ? *fallback
        : tmpdir() / "memo" / elle::system::username());
  }

  inline
  boost::filesystem::path
  xdg_state_home()
  {
    return _xdg_home("STATE", ".local/state");
  }

  using Headers = elle::unordered_map<std::string, std::string>;

  /// Whether this path's filename starts with `.` or ends with `~`.
  bool
  is_hidden_file(boost::filesystem::path const& path);

  /// Whether this entry is a non-hidden regular file.
  bool
  is_visible_file(boost::filesystem::directory_entry const& e);

  bool
  validate_email(std::string const& candidate);

  Headers
  signature_headers(
    elle::reactor::http::Method method,
    std::string const& where,
    User const& self,
    boost::optional<elle::ConstWeakBuffer> payload = {});

  template <typename Exception>
  ELLE_COMPILER_ATTRIBUTE_NORETURN
  void
  read_error(elle::reactor::http::Request& r,
             std::string const& type,
             std::string const& name);


  struct BeyondError
    : public elle::Error
  {
    BeyondError(std::string const& error,
                std::string const& reason,
                boost::optional<std::string> const& name = boost::none);
    BeyondError(elle::serialization::SerializerIn& s);

    std::string
    name_opt() const;

    ELLE_ATTRIBUTE_R(std::string, error);
    ELLE_ATTRIBUTE_R(boost::optional<std::string>, name);
  };
}

#include <infinit/utility.hxx>
