#include <sys/types.h>
#include <sys/stat.h>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <elle/Exit.hh>
#include <elle/Error.hh>
#include <elle/log.hh>
#include <elle/json/json.hh>

ELLE_LOG_COMPONENT("infinit-acl");

#include <main.hh>
#include <xattrs.hh>

#ifdef INFINIT_WINDOWS
# undef stat
#endif

infinit::Infinit ifnt;

namespace
{
  namespace bfs = boost::filesystem;

  constexpr char admin_prefix = '^';
  constexpr char group_prefix = '@';

  bool
  fallback_enabled(boost::program_options::variables_map const& args)
  {
#ifdef INFINIT_WINDOWS
    return true;
#else
    return flag(args, "fallback-xattrs");
#endif
  }

  bool
  is_admin(std::string const& obj)
  {
    return !obj.empty() && obj[0] == admin_prefix;
  }

  bool
  is_group(std::string const& obj)
  {
    return !obj.empty() && obj[0] == group_prefix;
  }


  using Strings = std::vector<std::string>;
  using OptStrings = boost::optional<Strings>;

  Strings
  collate_users(OptStrings&& combined,
                OptStrings&& users,
                OptStrings&& admins,
                OptStrings&& groups)
  {
    auto res = Strings{};
    if (combined)
      std::move(combined->begin(), combined->end(), std::back_inserter(res));
    if (users)
      std::move(users->begin(), users->end(), std::back_inserter(res));
    if (admins)
      for (auto a: admins.get())
        if (a[0] == admin_prefix)
          res.emplace_back(std::move(a));
        else
          res.emplace_back(elle::sprintf("%s%s", admin_prefix, a));
    if (groups)
      for (auto g: groups.get())
        if (g[0] == group_prefix)
          res.emplace_back(std::move(g));
        else
          res.emplace_back(elle::sprintf("%s%s", group_prefix, g));
    return res;
  }

  std::string
  public_key_from_username(std::string const& username, bool fetch)
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

  template<typename A, typename ... Args>
  void
  recursive_action(A action, std::string const& path, Args ... args)
  {
    boost::system::error_code erc;
    auto it = bfs::recursive_directory_iterator(path, erc);
    if (erc)
      elle::err("%s : %s", path, erc.message());
    for (; it != bfs::recursive_directory_iterator(); it.increment(erc))
    {
      // Ensure that we have permission on the file.
      bfs::exists(it->path(), erc);
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

  PermissionsResult
  get_acl(std::string const& path, bool fallback_xattrs)
  {
    PermissionsResult res;
    res.path = path;
    char buf[4096];
    int sz = port_getxattr(
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
      bool dir = bfs::is_directory(path);
      if (dir)
      {
        int sz = port_getxattr(
          path.c_str(), "user.infinit.auth.inherit", buf, 4095, fallback_xattrs);
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

  void
  set_action(std::string const& path,
             Strings users,
             std::string const& mode,
             std::string const& omode,
             bool inherit,
             bool disinherit,
             bool verbose,
             bool fallback_xattrs,
             bool fetch,
             bool multi = false)
  {
    if (verbose)
      std::cout << "processing " << path << std::endl;
    bool dir = bfs::is_directory(path);
    if (inherit || disinherit)
    {
      if (dir)
      {
        try
        {
          std::string value = inherit ? "true" : "false";
          check(port_setxattr, path, "user.infinit.auth.inherit", value,
                fallback_xattrs);
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
    if (!omode.empty())
    {
      try
      {
        check(port_setxattr, path, "user.infinit.auth_others", omode,
              fallback_xattrs);
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
    if (!mode.empty())
    {
      for (auto& username: users)
      {
        auto set_attribute =
          [path, mode, fallback_xattrs, multi] (std::string const& value)
          {
            try
            {
              check(port_setxattr, path, ("user.infinit.auth." + mode), value,
                    fallback_xattrs);
            }
            catch (PermissionDenied const&)
            {
              if (multi)
                std::cout << "permission denied, skipping " << path << std::endl;
              else
                std::cout << "permission denied " << path << std::endl;
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
            set_attribute(public_key_from_username(username, fetch));
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
}

COMMAND(set_xattr)
{
  auto path = mandatory<std::string>(args, "path", "target file/folder");
  auto name = mandatory<std::string>(args, "name", "attribute name");
  auto value = mandatory<std::string>(args, "value", "attribute value");
  port_setxattr(path, name, value, true);
}

COMMAND(get_xattr)
{
  auto path = mandatory<std::string>(args, "path", "target file/folder");
  auto name = mandatory<std::string>(args, "name", "attribute name");
  char result[16384];
  int res = port_getxattr(path, name, result, 16383, true);
  if (res < 0)
    perror("getxattr");
  else
  {
    result[res] = 0;
    std::cout << result << std::endl;
  }
}

COMMAND(list)
{
  auto paths = mandatory<Strings>(args, "path", "file/folder");
  if (paths.empty())
    elle::err<CommandLineError>("missing path argument");
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  bool fallback = fallback_enabled(args);
  for (auto const& path: paths)
  {
    enforce_in_mountpoint(path, fallback);
    if (script_mode)
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

namespace
{
  std::string
  get_mode(boost::optional<std::string> const& mode)
  {
    static auto const modes = std::map<std::string, std::string>
      {
        {"r", "setr"},
        {"w", "setw"},
        {"rw", "setrw"},
        {"none", "clear"},
        {"", ""},
      };
    auto i = modes.find(mode ? boost::algorithm::to_lower_copy(*mode) : "");
    if (i == modes.end())
      elle::err<CommandLineError>("invalid mode %s, must be one of: %s",
                                  mode, elle::keys(modes));
    else
      return i->second;
  }
}

COMMAND(set)
{
  auto paths = mandatory<Strings>(args, "path", "file/folder");
  if (paths.empty())
    elle::err<CommandLineError>("missing path argument");
  auto allowed_modes = Strings{"r", "w", "rw", "none", ""};
  auto omode = get_mode(optional(args, "others-mode"));
  auto users = collate_users(optional<Strings>(args, "user"),
                             boost::none,
                             boost::none,
                             optional<Strings>(args, "group"));
  auto mode = get_mode(optional(args, "mode"));
  if (!mode.empty() && users.empty())
    elle::err<CommandLineError>("must specify user when setting mode");
  if (!users.empty() && mode.empty())
    elle::err<CommandLineError>("must specify a mode for users");
  bool inherit = flag(args, "enable-inherit");
  bool disinherit = flag(args, "disable-inherit");
  if (inherit && disinherit)
    elle::err<CommandLineError>("inherit and disable-inherit are exclusive");
  if (!inherit && !disinherit && mode.empty() && omode.empty())
    elle::err<CommandLineError>("no operation specified");
  bool recursive = flag(args, "recursive");
  bool traverse = flag(args, "traverse");
  if (traverse && mode.find("setr"))
    elle::err("--traverse can only be used with mode 'r', 'rw'");
  bool verbose = flag(args, "verbose");
  bool fallback = fallback_enabled(args);
  bool fetch = flag(args, "fetch");
  // Don't do any operations before checking paths.
  for (auto const& path: paths)
  {
    enforce_in_mountpoint(path, fallback);
    if ((inherit || disinherit)
        && !recursive
        && !bfs::is_directory(path))
      elle::err<CommandLineError>("%s is not a directory, cannot %s inherit",
                                  path, inherit ? "enable" : "disable");
  }
  bool multi = paths.size() > 1 || recursive;
  for (auto const& path: paths)
  {
    set_action(path, users, mode, omode, inherit, disinherit, verbose,
               fallback, fetch, multi);
    if (traverse)
    {
      bfs::path working_path = bfs::absolute(path);
      while (!path_is_root(working_path.string(), fallback))
      {
        working_path = working_path.parent_path();
        set_action(working_path.string(), users, "setr", "", false, false,
                   verbose, fallback, fetch, multi);
      }
    }
    if (recursive)
    {
      recursive_action(
        set_action, path, users, mode, omode, inherit, disinherit,
        verbose, fallback, fetch, multi);
    }
  }
}

namespace
{
  void
  group_add_remove(std::string const& path,
                   std::string const& group,
                   std::string const& object,
                   std::string const& action,
                   bool fallback,
                   bool fetch)
  {
    if (!object.length())
      elle::err<CommandLineError>("empty user or group name");
    static const std::string base = "user.infinit.group.";
    std::string action_detail = is_admin(object) ? "admin" : "";
    std::string attr = elle::sprintf("%s%s%s", base, action, action_detail);
    auto set_attr = [&] (std::string const& identifier)
      {
        check(port_setxattr, path, attr, group + ":" + identifier, fallback);
      };
    std::string name = is_admin(object) ? object.substr(1) : object;
    try
    {
      set_attr(name);
    }
    catch (elle::Error const& e)
    {
      if (is_group(name))
        elle::err("ensure group \"%s\" exists and path is in a volume",
                  name.substr(1));
      else
      {
        try
        {
          set_attr(public_key_from_username(name, fetch));
        }
        catch (elle::Error const& e)
        {
          elle::err("ensure user \"%s\" exists and path is in a volume", name);
        }
      }
    }
  }
}

COMMAND(group)
{
  bool create = flag(args, "create");
  bool delete_ = flag(args, "delete");
  bool list = flag(args, "show");
  auto group = mandatory<std::string>(args, "name", "group name");
  auto description = optional<std::string>(args, "description");
  auto add = collate_users(optional<Strings>(args, "add"),
                           optional<Strings>(args, "add-user"),
                           optional<Strings>(args, "add-admin"),
                           optional<Strings>(args, "add-group"));
  auto rem = collate_users(optional<Strings>(args, "remove"),
                           optional<Strings>(args, "remove-user"),
                           optional<Strings>(args, "remove-admin"),
                           optional<Strings>(args, "remove-group"));
  int action_count = (create + delete_ + list
                      + !add.empty() + !rem.empty() + !!description);
  if (action_count == 0)
    elle::err<CommandLineError>("no action specified");
  if (action_count > 1)
    elle::err<CommandLineError>("specify only one action at a time");
  bool fallback = fallback_enabled(args);
  std::string path = mandatory<std::string>(args, "path", "path in volume");
  enforce_in_mountpoint(path, fallback);
  bool fetch = flag(args, "fetch");
  // Need to perform group actions on a directory in the volume.
  if (!bfs::is_directory(path))
    path = bfs::path(path).parent_path().string();
  if (create)
    check(port_setxattr, path, "user.infinit.group.create", group, fallback);
  if (delete_)
    check(port_setxattr, path, "user.infinit.group.delete", group, fallback);
  for (auto const& obj: add)
    group_add_remove(path, group, obj, "add", fallback, fetch);
  for (auto const& obj: rem)
      group_add_remove(path, group, obj, "remove", fallback, fetch);
  if (description)
    check(port_setxattr, path,
          elle::sprintf("infinit.groups.%s.description", group),
          description.get(), fallback);
  if (list)
  {
    char res[16384];
    int sz = port_getxattr(
      path, "user.infinit.group.list." + group, res, 16384, fallback);
    if (sz >= 0)
    {
      res[sz] = 0;
      std::cout << res << std::endl;
    }
    else
      elle::err("unable to list group: %s", group);
  }
}

COMMAND(register_)
{
  auto self = self_user(ifnt, args);
  auto user_name = mandatory<std::string>(args, "user", "user name");
  auto network_name = mandatory<std::string>(args, "network", "network name");
  auto network = ifnt.network_get(network_name, self);
  bool fallback = fallback_enabled(args);
  auto path = mandatory<std::string>(args, "path", "path to mountpoint");
  enforce_in_mountpoint(path, fallback);
  auto user = ifnt.user_get(user_name, flag(args, "fetch"));
  auto passport = ifnt.passport_get(network.name, user_name);
  std::stringstream output;
  elle::serialization::json::serialize(passport, output, false);
  check(port_setxattr, path, "user.infinit.register." + user_name, output.str(),
        fallback);
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionDescription fallback_option = {
    "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
    "if system xattrs are not suppported"
  };
  Mode::OptionDescription fetch_option = {
    "fetch", bool_switch(), "fetch users from " +
    infinit::beyond(true) +" if needed"
  };
  Mode::OptionDescription verbose_option = {
    "verbose", bool_switch(), "verbose output" };
  Modes modes {
    {
      "list",
      "List current ACL",
      &list,
      "--path PATHS",
      {
        { "path,p", value<Strings>(), "paths" },
        { "recursive,R", bool_switch(), "list recursively" },
        verbose_option,
      },
      {},
      {
        fallback_option,
      }
    },
    {
      "set",
      "Set ACL",
      &set,
      "--path PATHS [--user USERS]",
      {
        { "path,p", value<Strings>(), "paths" },
        { "user,u", value<Strings>()->multitoken(),
          elle::sprintf("users and groups (prefix: %s<group>)", group_prefix) },
        { "group,g", value<Strings>()->multitoken(),
          "groups" },
        { "mode,m", value<std::string>(), "access mode: r,w,rw,none" },
        { "others-mode,o", value<std::string>(),
          "access mode for other network users: r,w,rw,none" },
        { "enable-inherit,i", bool_switch(),
          "new files/folders inherit from their parent directory" },
        { "disable-inherit", bool_switch(),
          "new files/folders do not inherit from their parent directory" },
        { "recursive,R", bool_switch(), "apply recursively" },
        { "traverse", bool_switch(),
          "add read permissions to parent directories" },
        verbose_option,
        fetch_option,
      },
      {},
      {
        fallback_option,
      }
    },
    {
      "group",
      "Group control",
      &group,
      "--name NAME --path PATH",
      {
        { "name,n", value<std::string>(), "group name" },
        { "create,c", bool_switch(), "create the group" },
        option_description("group"),
        { "show", bool_switch(),
          "list group users, administrators and description" },
        // { "delete,d", bool_switch(), "delete an existing group" },
        { "add-user", value<Strings>(), "add user to group" },
        { "add-admin", value<Strings>(),
          "add administrator to group" },
        { "add-group", value<Strings>(),
          "add group to group" },
        { "add", value<Strings>()->multitoken(),
          elle::sprintf("add users, administrators and groups to group "
          "(prefix: %s<group>, %s<admin>)", group_prefix, admin_prefix) },
        { "remove-user", value<Strings>(),
          "remove user from group" },
        { "remove-admin", value<Strings>(),
          "remove administrator from group" },
        { "remove-group", value<Strings>(),
          "remove group from group" },
        { "remove", value<Strings>()->multitoken(),
          elle::sprintf("remove users, administrators and groups from group "
                        "(prefix: %s<group>, %s<admin>)",
                        group_prefix, admin_prefix) },
        { "path,p", value<std::string>(), "a path within the volume" },
        verbose_option,
        fetch_option,
      },
      {},
      {
        fallback_option,
      }
    },
    {
      "register",
      "Register user's passport to the network",
      &register_,
      "--user USER --network NETWORK --path PATH_TO_MOUNTPOINT",
      {
        { "user,u", value<std::string>(), "user to register"},
        { "path,p", value<std::string>(), "path to mountpoint" },
        { "network,N", value<std::string>(), "name of the network"},
        fetch_option,
      },
      {},
      {
        fallback_option,
      }
    }
  };
  Modes hidden_modes = {
    {
      "set-xattr",
      "Set an extended attribute",
      &set_xattr,
      "--path PATH --name NAME --value VALUE",
      {
        {"name,n", value<std::string>(), "attribute name"},
        {"value,s", value<std::string>(), "attribute value"},
        {"path,p", value<std::string>(), "Target file or directory"},
      },
    },
    {
      "get-xattr",
      "Get an extended attribute",
      &get_xattr,
      "--path PATH --name NAME",
      {
        {"name,n", value<std::string>(), "attribute name"},
        {"path,p", value<std::string>(), "Target file or directory"},
      },
    },
  };
  return infinit::main("Infinit access control list utility", modes, argc, argv,
                       std::string("path"), boost::none, hidden_modes);

}
