#include <sys/stat.h>

#include <infinit/cli/ACL.hh>

#include <elle/print.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/xattrs.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>

ELLE_LOG_COMPONENT("infinit-acl");

namespace infinit
{
  namespace cli
  {
    /*--------.
    | Helpers |
    `--------*/

    class PermissionDenied
      : public elle::Error
    {
    public:
      PermissionDenied(std::string const& error)
        : elle::Error(error)
      {}
    };

    class InvalidArgument
      : public elle::Error
    {
    public:
      InvalidArgument(std::string const& error)
        : elle::Error(error)
      {}
    };

    template<typename A, typename ... Args>
    void
    recursive_action(A action, std::string const& path, Args ... args)
    {
      namespace bfs = boost::filesystem;
      boost::system::error_code erc;
      bfs::recursive_directory_iterator it(path, erc);
      if (erc)
        throw elle::Error(elle::sprintf("%s : %s", path, erc.message()));
      for (; it != bfs::recursive_directory_iterator(); it.increment(erc))
      {
        // Ensure that we have permission on the file.
        boost::filesystem::exists(it->path(), erc);
        if (erc == boost::system::errc::permission_denied)
        {
          std::cout << "permission denied, skipping " << it->path().string()
                    << std::endl;
          continue;
        }
        action(it->path().string(), args...);
        reactor::yield();
      }
    }

    template<typename A, typename Res, typename ... Args>
    void
    recursive_action(std::vector<Res>& output,
                     A action,
                     std::string const& path, Args ... args)
    {
      recursive_action(
        [&] (std::string const& path) {
          output.push_back(action(path, args...));
        }, path);
    }

    static
    boost::optional<std::string>
    path_mountpoint(std::string const& path, bool fallback)
    {
      char buffer[4095];
      int sz = getxattr(path, "infinit.mountpoint", buffer, 4095, fallback);
      if (sz <= 0)
        return {};
      return std::string(buffer, sz);
    }

    static
    void
    enforce_in_mountpoint(std::string const& path_, bool fallback)
    {
      auto path = boost::filesystem::absolute(path_);
      if (!boost::filesystem::exists(path))
        elle::err(elle::sprintf("path does not exist: %s", path_));
      for (auto const& p: {path, path.parent_path()})
      {
        auto mountpoint = path_mountpoint(p.string(), fallback);
        if (mountpoint && !mountpoint.get().empty())
          return;
      }
      elle::err("%s not in an Infinit volume", path_);
    }

    struct PermissionsResult
    {
      // Factor this class.
      struct Permissions
      {
        Permissions() = default;
        Permissions(Permissions const&) = default;

        Permissions(elle::serialization::Serializer& s)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("read", this->read);
          s.serialize("write", this->write);
          s.serialize("owner", this->owner);
          s.serialize("admin", this->admin);
          s.serialize("name", this->name);
        }

        bool read;
        bool write;
        bool owner;
        bool admin;
        std::string name;
      };

      struct Directory
      {
        Directory()
          : inherit(boost::none)
        {}

        Directory(Directory const&) = default;

        Directory(elle::serialization::Serializer& s)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("inherit", this->inherit);
        }

        boost::optional<bool> inherit;
      };

      struct World
      {
        World() = default;
        World(World const&) = default;

        World(elle::serialization::Serializer& s)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("read", this->read);
          s.serialize("write", this->write);
        }

        bool read;
        bool write;
      };

      PermissionsResult() = default;
      PermissionsResult(PermissionsResult const&) = default;

      PermissionsResult(elle::serialization::Serializer& s)
      {
        this->serialize(s);
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("path", this->path);
        s.serialize("error", this->error);
        s.serialize("permissions", this->permissions);
        s.serialize("directory", this->directory);
        s.serialize("world", this->world);
      }

      std::string path;
      boost::optional<std::string> error;
      std::vector<Permissions> permissions;
      boost::optional<Directory> directory;
      boost::optional<World> world;
    };

    static
    PermissionsResult
    get_acl(std::string const& path, bool fallback_xattrs)
    {
      PermissionsResult res;
      res.path = path;
      char buf[4096];
      int sz = getxattr(
        path.c_str(), "user.infinit.auth", buf, 4095, fallback_xattrs);
      if (sz < 0)
      {
        auto err = errno;
        res.error = std::string{std::strerror(err)};
      }
      else
      {
        buf[sz] = 0;
        std::stringstream ss;
        ss.str(buf);
#ifndef __clang__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        boost::optional<bool> dir_inherit;
#ifndef __clang__
# pragma GCC diagnostic pop
#endif
        bool dir = boost::filesystem::is_directory(path);
        if (dir)
        {
          int sz = getxattr(
            path.c_str(), "user.infinit.auth.inherit",
            buf, 4095, fallback_xattrs);
          if (sz < 0)
            perror(path.c_str());
          else
          {
            buf[sz] = 0;
            if (buf == std::string("true"))
              dir_inherit = true;
            else if (buf == std::string("false"))
              dir_inherit = false;
          }
        }
        try
        {
          elle::json::Json j = elle::json::read(ss);
          auto a = boost::any_cast<elle::json::Array>(j);
          for (auto const& _entry: a)
          {
            auto const& entry = boost::any_cast<elle::json::Object>(_entry);
            PermissionsResult::Permissions perms;
            perms.read = boost::any_cast<bool>(entry.at("read"));
            perms.write = boost::any_cast<bool>(entry.at("write"));
            perms.admin = boost::any_cast<bool>(entry.at("admin"));
            perms.owner = boost::any_cast<bool>(entry.at("owner"));
            perms.name = boost::any_cast<std::string>(entry.at("name"));
            res.permissions.push_back(perms);
          }
          if (dir)
          {
            PermissionsResult::Directory dir;
            if (dir_inherit)
              dir.inherit = *dir_inherit;
            res.directory = dir;
          }
          {
            struct stat st;
            int stat_result = ::stat(path.c_str(),&st);
            if (stat_result != 0)
              perror(path.c_str());
            else
            {
              if (st.st_mode & 06)
              {
                PermissionsResult::World world;
                world.read = true;
                world.write = st.st_mode & 02;
                res.world = world;
              }
            }
          }
          return res;
        }
        catch (reactor::Terminate const&)
        {
          throw;
        }
        catch (...)
        {
          res.error = elle::exception_string();
        }
      }
      return res;
    }

    static
    void
    list_action(std::string const& path, bool verbose, bool fallback_xattrs)
    {
      if (verbose)
        std::cout << "processing " << path << std::endl;
      auto res = get_acl(path, fallback_xattrs);
      if (res.error)
        std::cerr << path << ": " << *res.error
                  << std::endl;
      else
      {
        std::stringstream output;
        output << path << ":" << std::endl;
        if (res.directory)
        {
          auto dir = *res.directory;
          output << "  inherit: "
                 << (dir.inherit ? dir.inherit.get() ? "true" : "false" : "unknown")
                 << std::endl;
        }
        if (res.world)
        {
          output << "  world: "
                 << ((*res.world).write ? "rw" : "r")
                 << std::endl;
        }
        for (auto& perm: res.permissions)
        {
          const char* mode = perm.write
            ? (perm.read ? "rw" : "w")
            : (perm.read ? "r" : "none");
          output << "    " << perm.name;
          if (perm.admin || perm.owner)
          {
            output << " (";
            if (perm.admin)
              output << "admin";
            if (perm.admin && perm.owner)
              output << ", ";
            if (perm.owner)
              output << "owner";
            output << ")";
          }
          output << ": " << mode << std::endl;
        }
        std::cout << output.str();
      }
    }

    static char const admin_prefix = '^';
    static char const group_prefix = '@';

    static
    bool
    is_admin(std::string const& obj)
    {
      return obj.length() > 0 && obj[0] == admin_prefix;
    }

    static
    bool
    is_group(std::string const& obj)
    {
      return obj.length() > 0 && obj[0] == group_prefix;
    }

    static
    boost::optional<std::vector<std::string>>
    collate_users(boost::optional<std::vector<std::string>> const& combined,
                  boost::optional<std::vector<std::string>> const& users,
                  boost::optional<std::vector<std::string>> const& admins,
                  boost::optional<std::vector<std::string>> const& groups)
    {
      if (!combined && !users && !admins && !groups)
        return boost::none;
      std::vector<std::string> res;
      if (combined)
      {
        for (auto c: combined.get())
          res.push_back(c);
      }
      if (users)
      {
        for (auto u: users.get())
          res.push_back(u);
      }
      if (admins)
      {
        for (auto a: admins.get())
        {
          if (a[0] == admin_prefix)
            res.push_back(a);
          else
            res.push_back(elle::sprintf("%s%s", admin_prefix, a));
        }
      }
      if (groups)
      {
        for (auto g: groups.get())
        {
          if (g[0] == group_prefix)
            res.push_back(g);
          else
            res.push_back(elle::sprintf("%s%s", group_prefix, g));
        }
      }
      return res;
    }

    static
    std::string
    public_key_from_username(infinit::Infinit& ifnt,
                             std::string const& username, bool fetch)
    {
      auto user = ifnt.user_get(username, fetch);
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        elle::serialization::json::SerializerOut so(ios, false);
        so.serialize_forward(user.public_key);
      }
      return buf.string();
    }

    static
    void
    group_add_remove(infinit::Infinit& infinit,
                     boost::filesystem::path const& path,
                     std::string const& group,
                     std::string const& object,
                     std::string const& action,
                     bool fallback,
                     bool fetch)
    {
      if (!object.length())
        elle::err<CLIError>("empty user or group name");
      static const std::string base = "user.infinit.group.";
      std::string action_detail = is_admin(object) ? "admin" : "";
      std::string attr = elle::sprintf("%s%s%s", base, action, action_detail);
      auto set_attr = [&] (std::string const& identifier)
        {
          setxattr(path.string(), attr, group + ":" + identifier, fallback);
        };
      std::string name = is_admin(object) ? object.substr(1) : object;
      try
      {
        set_attr(name);
      }
      catch (elle::Error const& e)
      {
        if (is_group(name))
        {
          throw elle::Error(elle::sprintf(
            "ensure group \"%s\" exists and path is in a volume", name.substr(1)));
        }
        else
        {
          try
          {
            set_attr(public_key_from_username(infinit, name, fetch));
          }
          catch (elle::Error const& e)
          {
            throw elle::Error(elle::sprintf(
              "ensure user \"%s\" exists and path is in a volume", name));
          }
        }
      }
    }

    /*----.
    | ACL |
    `----*/

    using Error = das::cli::Error;

    ACL::ACL(Infinit& infinit)
      : Entity(infinit)
      , get_xattr(
        "Get an extended attribute value",
        das::cli::Options(),
        this->bind(modes::mode_get_xattr,
                   cli::path,
                   cli::name))
      , group(
        "Edit groups",
        das::cli::Options(),
        this->bind(modes::mode_group,
                   cli::path,
                   cli::name,
                   cli::create = false,
                   cli::delete_ = false,
                   cli::show = false,
                   cli::description = boost::none,
                   cli::add_user = std::vector<std::string>(),
                   cli::add_group = std::vector<std::string>(),
                   cli::add_admin = std::vector<std::string>(),
                   cli::add = std::vector<std::string>(),
                   cli::remove_user = std::vector<std::string>(),
                   cli::remove_group = std::vector<std::string>(),
                   cli::remove_admin = std::vector<std::string>(),
                   cli::remove = std::vector<std::string>(),
                   cli::verbose = false,
                   cli::fetch = false,
                   cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                   true
#else
                   false
#endif
          ))
      , list(
        "List ACLs",
        das::cli::Options(),
        this->bind(modes::mode_list,
                   cli::path,
                   cli::recursive = false,
                   cli::verbose = false,
                   cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                   true
#else
                   false
#endif
          ))
      , set(
        "Set ACLs",
        das::cli::Options(),
        this->bind(modes::mode_set,
                   cli::path,
                   cli::user,
                   cli::group,
                   cli::mode = boost::none,
                   cli::others_mode = boost::none,
                   // FIXME: change that to just "inherit"
                   cli::enable_inherit = false,
                   cli::disable_inherit = false,
                   cli::recursive = false,
                   cli::traverse = false,
                   cli::verbose = false,
                   cli::fetch = false,
                   cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                   true
#else
                   false
#endif
          ))
      , register_(
        "Register user's passport to the network",
        das::cli::Options(),
        this->bind(modes::mode_register,
                   cli::path,
                   cli::user,
                   cli::network,
                   cli::fetch = false,
                   cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                   true
#else
                   false
#endif
          ))
      , set_xattr(
        "Set an extended attribute value",
        das::cli::Options(),
        this->bind(modes::mode_set_xattr,
                   cli::path,
                   cli::name,
                   cli::value))
    {}

    /*------------.
    | Mode: group |
    `------------*/

    void
    ACL::mode_group(std::string const& path_str,
                    std::string const& group,
                    bool create,
                    bool delete_,
                    bool show,
                    boost::optional<std::string> description,
                    std::vector<std::string> add_user,
                    std::vector<std::string> add_group,
                    std::vector<std::string> add_admin,
                    std::vector<std::string> add_,
                    std::vector<std::string> rem_user,
                    std::vector<std::string> rem_group,
                    std::vector<std::string> rem_admin,
                    std::vector<std::string> rem_,
                    bool verbose,
                    bool fetch,
                    bool fallback)
    {
      boost::filesystem::path path(path_str);
      auto add = collate_users(add_, add_user, add_admin, add_group);
      auto rem = collate_users(rem_, rem_user, rem_admin, rem_group);
      int action_count = (create ? 1 : 0) + (delete_ ? 1 : 0) + (show ? 1 : 0)
        + (add ? 1 : 0) + (rem ? 1 : 0) + (description ? 1 : 0);
      if (action_count == 0)
        elle::err<CLIError>("no action specified");
      if (action_count > 1)
        elle::err<CLIError>("specify only one action at a time");
      enforce_in_mountpoint(path.string(), fallback);
      // Need to perform group actions on a directory in the volume.
      if (!is_directory(path))
        path = path.parent_path();
      if (create)
        setxattr(path.string(), "user.infinit.group.create", group, fallback);
      if (delete_)
        setxattr(path.string(), "user.infinit.group.delete", group, fallback);
      if (add)
      {
        for (auto const& obj: add.get())
          group_add_remove(this->cli().infinit(),
                           path, group, obj, "add", fallback, fetch);
      }
      if (rem)
      {
        for (auto const& obj: rem.get())
          group_add_remove(this->cli().infinit(),
                           path, group, obj, "remove", fallback, fetch);
      }
      if (description)
      {
        setxattr(path.string(),
                 elle::sprintf("infinit.groups.%s.description", group),
                 description.get(), fallback);
      }
      if (show)
      {
        char res[16384];
        int sz = getxattr(
          path.string(), "user.infinit.group.list." + group,
          res, 16384, fallback);
        if (sz >= 0)
        {
          res[sz] = 0;
          std::cout << res << std::endl;
        }
        else
        {
          throw elle::Error(elle::sprintf("unable to list group: %s", group));
        }
      }
    }

    /*----------------.
    | Mode: get_xattr |
    `----------------*/

    void
    ACL::mode_get_xattr(std::string const& path,
                        std::string const& name)
    {
      char result[16384];
      int length = getxattr(path, name, result, sizeof(result) - 1, true);
      result[length] = 0;
      std::cout << result << std::endl;
    }

    /*-----------.
    | Mode: list |
    `-----------*/

    void
    ACL::mode_list(std::vector<std::string> const& paths,
                   bool recursive,
                   bool verbose,
                   bool fallback)
    {
      if (paths.empty())
        throw das::cli::MissingOption("path");
      for (auto const& path: paths)
      {
        enforce_in_mountpoint(path, fallback);
        if (this->cli().script())
        {
          auto permissions = std::vector<PermissionsResult>();
          permissions.push_back(get_acl(path, fallback));
          if (recursive)
            recursive_action(permissions, get_acl, path, fallback);
          elle::serialization::json::serialize(permissions, std::cout, false);
        }
        else
        {
          list_action(path, verbose, fallback);
          if (recursive)
            recursive_action(list_action, path, verbose, fallback);
        }
      }
    }

    /*---------------.
    | Mode: register |
    `---------------*/

    void
    ACL::mode_register(std::string const& path,
                       std::string const& user_name,
                       std::string const& network_name,
                       bool fetch,
                       bool fallback)
    {
      auto& ifnt = this->cli().infinit();
      auto self = this->cli().as_user();
      auto network = ifnt.network_get(network_name, self);
      enforce_in_mountpoint(path, fallback);
      auto user = ifnt.user_get(user_name, fetch);
      auto passport = ifnt.passport_get(network.name, user_name);
      std::stringstream output;
      elle::serialization::json::serialize(passport, output, false);
      setxattr(path, "user.infinit.register." + user_name, output.str(),
               fallback);
    }

    /*----------.
    | Mode: set |
    `----------*/

    static std::unordered_map<std::string, std::string> const modes_map = {
      {"r",    "setr"},
      {"w",    "setw"},
      {"rw",   "setrw"},
      {"none", "clear"},
      {"",     ""},
    };

    static
    bool
    path_is_root(std::string const& path, bool fallback)
    {
      char buffer[4095];
      int sz = getxattr(path, "infinit.root", buffer, 4095, fallback);
      if (sz < 0)
        return false;
      return std::string(buffer, sz) == std::string("true");
    }

    static
    void
    set_action(std::string const& path,
               infinit::Infinit& infinit,
               std::vector<std::string> users,
               boost::optional<std::string> mode,
               boost::optional<std::string> omode,
               bool inherit,
               bool disinherit,
               bool verbose,
               bool fallback_xattrs,
               bool fetch,
               bool multi = false)
    {
      if (verbose)
        elle::print(std::cout, "processing {}\n", path);
      using namespace boost::filesystem;
      bool dir = is_directory(path);
      if (inherit || disinherit)
      {
        if (dir)
        {
          try
          {
            std::string value = inherit ? "true" : "false";
            setxattr(path, "user.infinit.auth.inherit", value, fallback_xattrs);
          }
          catch (PermissionDenied const&)
          {
            if (multi)
              std::cout << "permission denied, skipping " << path << std::endl;
            else
              std::cout << "permission denied " << path << std::endl;
          }
          catch (elle::Error const& error)
          {
            ELLE_ERR("setattr (inherit) on %s failed: %s", path,
                     elle::exception_string());
          }
        }
      }
      if (omode)
      {
        try
        {
          setxattr(path, "user.infinit.auth_others",
                   modes_map.at(omode.get()), fallback_xattrs);
        }
        catch (PermissionDenied const&)
        {
          if (multi)
            std::cout << "permission denied, skipping " << path << std::endl;
          else
            std::cout << "permission denied " << path << std::endl;
        }
        catch (InvalidArgument const&)
        {
          ELLE_ERR("setattr (omode: %s) on %s failed: %s", omode, path,
                   elle::exception_string());
          throw;
        }
      }
      if (mode)
      {
        for (auto& username: users)
        {
          auto set_attribute =
            [path, mode, fallback_xattrs, multi] (std::string const& value)
            {
              try
              {
                setxattr(path, "user.infinit.auth." + modes_map.at(mode.get()),
                         value, fallback_xattrs);
              }
              catch (PermissionDenied const&)
              {
                if (multi)
                  elle::print(std::cout,
                              "permission denied, skipping {}\n", path);
                else
                  elle::print(std::cout,
                              "permission denied on {}\n", path);
              }
            };
          try
          {
            set_attribute(username);
          }
          // XXX: Invalid argument could be something else... Find a way to
          // distinguish the different errors.
          catch (InvalidArgument const&)
          {
            try
            {
              set_attribute(public_key_from_username(
                              infinit, username, fetch));
            }
            catch (InvalidArgument const&)
            {
              ELLE_ERR("setattr (mode: %s) on %s failed: %s", mode, path,
                       elle::exception_string());
              throw;
            }
          }
        }
      }
    }

    void
    ACL::mode_set(std::vector<std::string> const& paths,
                  std::vector<std::string> const& users,
                  std::vector<std::string> const& groups,
                  boost::optional<std::string> mode,
                  boost::optional<std::string> others_mode,
                  bool inherit,
                  bool disinherit,
                  bool recursive,
                  bool traverse,
                  bool verbose,
                  bool fetch,
                  bool fallback)
    {
      if (paths.empty())
        throw das::cli::MissingOption("path");
      static auto const check_mode = [] (boost::optional<std::string> const& m)
        {
          if (m)
          {
            auto mode = m.get();
            std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
            if (modes_map.find(mode) == modes_map.end())
              elle::err<das::cli::Error>("invalide mode: %s", m);
          }
        };
      check_mode(mode);
      check_mode(others_mode);
      auto combined = collate_users(users, boost::none, boost::none, groups);
      // auto users = combined ? combined.get() : std::vector<std::string>();
      if (mode && !combined)
        elle::err<das::cli::Error>("must specify user when setting mode");
      if (!mode && combined)
        throw das::cli::MissingOption("mode");
      if (inherit && disinherit)
        elle::err<das::cli::Error>("inherit and disable-inherit are exclusive");
      if (!inherit && !disinherit && !mode && !others_mode)
        elle::err<das::cli::Error>("no operation specified");
      if (traverse && mode && mode->find("r") == std::string::npos)
        elle::err<das::cli::Error>(
          "--traverse can only be used with mode 'r' or 'rw'");
      // Don't do any operations before checking paths.
      for (auto const& path: paths)
      {
        enforce_in_mountpoint(path, fallback);
        if ((inherit || disinherit)
            && !recursive
            && !boost::filesystem::is_directory(path))
        {
          elle::err("%s is not a directory, cannot %s inheritance",
                    path, inherit ? "enable" : "disable");
        }
      }
      bool multi = paths.size() > 1 || recursive;
      for (auto const& path: paths)
      {
        set_action(path, this->cli().infinit(), users, mode, others_mode,
                   inherit, disinherit, verbose, fallback, fetch, multi);
        if (traverse)
        {
          boost::filesystem::path working_path = boost::filesystem::absolute(path);
          while (!path_is_root(working_path.string(), fallback))
          {
            working_path = working_path.parent_path();
            set_action(working_path.string(), this->cli().infinit(), users,
                       std::string("r"), boost::none, false, false,
                       verbose, fallback, fetch, multi);
          }
        }
        if (recursive)
        {
          recursive_action(
            set_action, path, this->cli().infinit(), users, mode, others_mode,
            inherit, disinherit, verbose, fallback, fetch, multi);
        }
      }
    }

    /*----------------.
    | Mode: set_xattr |
    `----------------*/

    void
    ACL::mode_set_xattr(std::string const& path,
                        std::string const& name,
                        std::string const& value)
    {
      setxattr(path, name, value, true);
    }
  }
}
