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

#include <memo/User.hh>
#include <memo/environ.hh>

namespace memo
{
  using namespace std::literals;
  namespace bfs = boost::filesystem;

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
  bfs::path
  canonical_folder(bfs::path const& path)
  {
    if (exists(path) && !is_directory(path))
      elle::err("not a directory: %s", path);
    create_directories(path);
    boost::system::error_code erc;
    permissions(
      path, bfs::add_perms | bfs::owner_write, erc);
    return canonical(path);
  }

  inline
  bfs::path
  home()
  {
    auto const memo_home = memo::getenv("HOME", ""s);
    return memo_home.empty() ? elle::system::home_directory() : memo_home;
  }

  inline
  bfs::path
  _xdg(std::string const& type,
       bfs::path const& def)
  {
    auto const memo = memo::getenv(type, ""s);
    auto const xdg = elle::os::getenv("XDG_" + type, ""s);
    auto const dir =
      !memo.empty() ? memo :
      !xdg.empty() ? bfs::path(xdg) / "infinit/memo" :
      def;
    try
    {
      return canonical_folder(dir);
    }
    catch (bfs::filesystem_error& e)
    {
      ELLE_LOG_COMPONENT("xdg");
      std::string env =
        !memo.empty() ? "MEMO_" : !xdg.empty() ? "XDG_" : "";
      if (!env.empty())
        ELLE_WARN("Invalid \"%s%s\" directory: %s", env, type, e.what());
      elle::err(e.what());
    }
  }

  inline
  bfs::path
  _xdg_home(std::string const& type,
            bfs::path const& def)
  {
    return _xdg(type + "_HOME", home() / def / "infinit/memo");
  }

  inline
  bfs::path
  xdg_cache_home()
  {
    return _xdg_home("CACHE", ".cache");
  }

  inline
  bfs::path
  xdg_config_home()
  {
    return _xdg_home("CONFIG", ".config");
  }

  inline
  bfs::path
  xdg_data_home()
  {
    return _xdg_home("DATA", ".local/share");
  }

  inline
  bfs::path
  tmpdir()
  {
    return elle::os::getenv("TMPDIR", "/tmp");;
  }

  inline
  bfs::path
  xdg_runtime_dir(boost::optional<std::string> fallback = {})
  {
    return _xdg(
      "RUNTIME_DIR",
      fallback
        ? *fallback
        : tmpdir() / "memo" / elle::system::username());
  }

  inline
  bfs::path
  xdg_state_home()
  {
    return _xdg_home("STATE", ".local/state");
  }

  using Headers = elle::unordered_map<std::string, std::string>;

  /// Whether this path's filename starts with `.` or ends with `~`.
  bool
  is_hidden_file(bfs::path const& path);

  /// Whether this entry is a non-hidden regular file.
  bool
  is_visible_file(bfs::directory_entry const& e);

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

  /// Backward compatibility: serialization of some duration variables
  /// `foo` as `foo_ms`, number of ms.
  template <typename Serializer, typename Duration>
  void
  serialize_duration_ms(Serializer& s, std::string const& name, Duration& d)
  {
    using milliseconds = std::chrono::milliseconds;
    if (s.out())
    {
      int ms = std::chrono::duration_cast<milliseconds>(d).count();
      s.serialize(name + "_ms", ms);
    }
    else
    {
      int ms;
      s.serialize(name + "_ms", ms);
      d = milliseconds(ms);
    }
  };
}

#include <memo/utility.hxx>
