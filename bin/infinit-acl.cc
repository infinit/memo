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

static const char admin_prefix = '^';
static const char group_prefix = '@';

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

typedef boost::optional<std::vector<std::string>> OptVecStr;

static
OptVecStr
collate_users(OptVecStr const& combined,
              OptVecStr const& users,
              OptVecStr const& admins,
              OptVecStr const& groups)
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

static
boost::optional<std::string>
path_mountpoint(std::string const& path, bool fallback)
{
  char buffer[4095];
  int sz = port_getxattr(path, "infinit.mountpoint", buffer, 4095, fallback);
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
    throw elle::Error(elle::sprintf("path does not exist: %s", path_));
  for (auto const& p: {path, path.parent_path()})
  {
    auto mountpoint = path_mountpoint(p.string(), fallback);
    if (mountpoint && !mountpoint.get().empty())
      return;
  }
  throw elle::Error(elle::sprintf("%s not in an Infinit volume", path_));
}

static
bool
path_is_root(std::string const& path, bool fallback)
{
  char buffer[4095];
  int sz = port_getxattr(path, "infinit.root", buffer, 4095, fallback);
  if (sz < 0)
    return false;
  return std::string(buffer, sz) == std::string("true");
}

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

template<typename F, typename ... Args>
void
check(F func, Args ... args)
{
  int res = func(args...);
  if (res < 0)
  {
    int error_number = errno;
    auto* e = std::strerror(error_number);
    if (error_number == EINVAL)
      throw InvalidArgument(std::string(e));
    else if (error_number == EACCES)
      throw PermissionDenied(std::string(e));
    else
      throw elle::Error(std::string(e));
  }
}

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
  }
}

static
void
list_action(std::string const& path, bool verbose, bool fallback_xattrs)
{
  if (verbose)
    std::cout << "processing " << path << std::endl;
  bool dir = boost::filesystem::is_directory(path);
  char buf[4096];
  int sz = port_getxattr(
    path.c_str(), "user.infinit.auth", buf, 4095, fallback_xattrs);
  if (sz < 0)
    perror(path.c_str());
  else
  {
    buf[sz] = 0;
    std::stringstream ss;
    ss.str(buf);
    boost::optional<bool> dir_inherit;
    if (dir)
    {
      int sz = port_getxattr(
        path.c_str(), "user.infinit.auth.inherit", buf, 4095, fallback_xattrs);
      if (sz < 0)
        perror(path.c_str());
      else
      {
        buf[sz] = 0;
        dir_inherit = (buf == std::string("true"));
      }
    }
    try
    {
      // [{name: s, read: bool, write: bool}]
      std::stringstream output;
      output << path << ":" << std::endl;
      if (dir_inherit)
      {
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        output << "  inherit: "
               << (dir_inherit.get() ? "yes" : "no")
               << std::endl;
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif
      }
      struct stat st;
      int res = ::stat(path.c_str(),&st);
      if (res != 0)
        perror(path.c_str());
      else
      {
        if (st.st_mode & 06)
        {
          output << "  world: " << ((st.st_mode & 02) ? "rw" : "r")
                 << std::endl;
        }
      }
      elle::json::Json j = elle::json::read(ss);
      auto a = boost::any_cast<elle::json::Array>(j);
      for (auto& li: a)
      {
        auto d = boost::any_cast<elle::json::Object>(li);
        auto n = boost::any_cast<std::string>(d.at("name"));
        auto r = boost::any_cast<bool>(d.at("read"));
        auto w = boost::any_cast<bool>(d.at("write"));
        const char* mode = w ? (r ? "rw" : "w") : (r ? "r" : "none");
        output << "\t" << n << ": " << mode << std::endl;
      }
      std::cout << output.str() << std::endl;
    }
    catch (std::exception const& e)
    {
      std::cout << path << " : " << buf << std::endl;
    }
  }
}

static
void
set_action(std::string const& path,
           std::vector<std::string> users,
           std::string const& mode,
           std::string const& omode,
           bool inherit,
           bool disinherit,
           bool verbose,
           bool fallback_xattrs,
           bool fetch)
{
  if (verbose)
    std::cout << "processing " << path << std::endl;
  using namespace boost::filesystem;
  bool dir = is_directory(path);
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
        std::cout << "permission denied, skipping " << path << std::endl;
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
      std::cout << "permission denied, skipping " << path << std::endl;
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
        [path, mode, fallback_xattrs] (std::string const& value)
        {
          try
          {
            check(port_setxattr, path, ("user.infinit.auth." + mode), value,
                  fallback_xattrs);
          }
          catch (PermissionDenied const&)
          {
            std::cout << "permission denied, skipping " << path << std::endl;
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
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  bool fallback = flag(args, "fallback-xattrs");
  for (auto const& path: paths)
  {
    enforce_in_mountpoint(path, fallback);
    list_action(path, verbose, fallback);
    if (recursive)
      recursive_action(list_action, path, verbose, fallback);
  }
}

COMMAND(set)
{
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  std::vector<std::string> allowed_modes = {"r", "w", "rw", "none", ""};
  auto omode_ = optional(args, "others-mode");
  auto omode = omode_? omode_.get() : "";
  auto it = std::find(allowed_modes.begin(), allowed_modes.end(), omode);
  if (it == allowed_modes.end())
  {
    throw CommandLineError(
      elle::sprintf("mode must be one of: %s", allowed_modes));
  }
  auto users_ = optional<std::vector<std::string>>(args, "user");
  auto groups = optional<std::vector<std::string>>(args, "group");
  auto combined = collate_users(users_, boost::none, boost::none, groups);
  auto users = combined ? combined.get() : std::vector<std::string>();
  auto mode_ = optional(args, "mode");
  auto mode = mode_ ? mode_.get() : "";
  it = std::find(allowed_modes.begin(), allowed_modes.end(), mode);
  if (it == allowed_modes.end())
  {
    throw CommandLineError(
      elle::sprintf("mode must be one of: %s", allowed_modes));
  }
  if (!mode.empty() && users.empty())
    throw CommandLineError("must specify user when setting mode");
  if (!users.empty() && mode.empty())
    throw CommandLineError("must specify a mode for users");
  bool inherit = flag(args, "enable-inherit");
  bool disinherit = flag(args, "disable-inherit");
  if (inherit && disinherit)
  {
    throw CommandLineError(
      "inherit and disable-inherit are exclusive");
  }
  if (!inherit && !disinherit && mode.empty() && omode.empty())
    throw CommandLineError("no operation specified");
  std::vector<std::string> modes_map = {"setr", "setw", "setrw", "clear", ""};
  mode = modes_map[it - allowed_modes.begin()];
  bool recursive = flag(args, "recursive");
  bool traverse = flag(args, "traverse");
  if (traverse && mode.find("setr") != 0)
    throw elle::Error("--traverse can only be used with mode 'r', 'rw'");
  bool verbose = flag(args, "verbose");
  bool fallback = flag(args, "fallback-xattrs");
  bool fetch = flag(args, "fetch");
  // Don't do any operations before checking paths.
  for (auto const& path: paths)
  {
    enforce_in_mountpoint(path, fallback);
    if ((inherit || disinherit)
        && !recursive
        && !boost::filesystem::is_directory(path))
    {
      throw CommandLineError(elle::sprintf(
        "%s is not a directory, cannot %s inherit",
        path, inherit ? "enable" : "disable"));
    }
  }
  for (auto const& path: paths)
  {
    set_action(path, users, mode, omode, inherit, disinherit, verbose,
               fallback, fetch);
    if (traverse)
    {
      boost::filesystem::path working_path = boost::filesystem::absolute(path);
      while (!path_is_root(working_path.string(), fallback))
      {
        working_path = working_path.parent_path();
        set_action(working_path.string(), users, "setr", "", false, false,
                   verbose, fallback, fetch);
      }
    }
    if (recursive)
    {
      recursive_action(
        set_action, path, users, mode, omode, inherit, disinherit, verbose,
        fallback, fetch);
    }
  }
}

static
void
group_add_remove(std::string const& path,
                 std::string const& group,
                 std::string const& object,
                 std::string const& action,
                 bool fallback,
                 bool fetch)
{
  if (!object.length())
    throw CommandLineError("empty user or group name");
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
    {
      throw elle::Error(elle::sprintf(
        "ensure group \"%s\" exists and path is in a volume", name.substr(1)));
    }
    else
    {
      try
      {
        set_attr(public_key_from_username(name, fetch));
      }
      catch (elle::Error const& e)
      {
        throw elle::Error(elle::sprintf(
          "ensure user \"%s\" exists and path is in a volume", name));
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
  auto add_user = optional<std::vector<std::string>>(args, "add-user");
  auto add_admin = optional<std::vector<std::string>>(args, "add-admin");
  auto add_group = optional<std::vector<std::string>>(args, "add-group");
  auto add = optional<std::vector<std::string>>(args, "add");
  add = collate_users(add, add_user, add_admin, add_group);
  auto rem_user = optional<std::vector<std::string>>(args, "remove-user");
  auto rem_admin = optional<std::vector<std::string>>(args, "remove-admin");
  auto rem_group = optional<std::vector<std::string>>(args, "remove-group");
  auto rem = optional<std::vector<std::string>>(args, "remove");
  rem = collate_users(rem, rem_user, rem_admin, rem_group);
  int action_count = (create ? 1 : 0) + (delete_ ? 1 : 0) + (list ? 1 : 0)
                   + (add ? 1 : 0) + (rem ? 1 : 0);
  if (action_count == 0)
    throw CommandLineError("no action specified");
  if (action_count > 1)
    throw CommandLineError("specify only one action at a time");
  bool fallback = flag(args, "fallback-xattrs");
  std::string path = mandatory<std::string>(args, "path", "path in volume");
  enforce_in_mountpoint(path, fallback);
  bool fetch = flag(args, "fetch");
  // Need to perform group actions on a directory in the volume.
  if (!boost::filesystem::is_directory(path))
    path = boost::filesystem::path(path).parent_path().string();
  if (create)
    check(port_setxattr, path, "user.infinit.group.create", group, fallback);
  if (delete_)
    check(port_setxattr, path, "user.infinit.group.delete", group, fallback);
  if (add)
  {
    for (auto const& obj: add.get())
      group_add_remove(path, group, obj, "add", fallback, fetch);
  }
  if (rem)
  {
    for (auto const& obj: rem.get())
      group_add_remove(path, group, obj, "remove", fallback, fetch);
  }
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
    {
      throw elle::Error(elle::sprintf("unable to list group: %s", group));
    }
  }
}

COMMAND(register_)
{
  auto self = self_user(ifnt, args);
  auto user_name = mandatory<std::string>(args, "user", "user name");
  auto network_name = mandatory<std::string>(args, "network", "network name");
  auto network = ifnt.network_get(network_name, self);
  bool fallback = flag(args, "fallback-xattrs");
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
  program = argv[0];
  Mode::OptionDescription fallback_option = {
    "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
    "if system xattrs are not suppported"
  };
  Mode::OptionDescription fetch_option = {
    "fetch", bool_switch(), "fetch users from " + beyond(true) +" if needed"
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
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "recursive,R", bool_switch(), "list recursively" },
        fallback_option,
        verbose_option,
      },
    },
    {
      "set",
      "Set ACL",
      &set,
      "--path PATHS [--user USERS]",
      {
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "user,u", value<std::vector<std::string>>()->multitoken(),
          elle::sprintf("users and groups (prefix: %s<group>)", group_prefix) },
        { "group,g", value<std::vector<std::string>>(), "groups" },
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
        fallback_option,
        verbose_option,
        fetch_option,
      },
    },
    {
      "group",
      "Group control",
      &group,
      "--name NAME --path PATH",
      {
        { "name,n", value<std::string>(), "group name" },
        { "create,c", bool_switch(), "create the group" },
        { "show", bool_switch(), "list group users and administrators" },
        { "delete,d", bool_switch(), "delete an existing group" },
        { "add-user", value<std::vector<std::string>>(), "add user to group" },
        { "add-admin", value<std::vector<std::string>>(),
          "add administrator to group" },
        { "add-group", value<std::vector<std::string>>(),
          "add group to group" },
        { "add", value<std::vector<std::string>>()->multitoken(),
          elle::sprintf("add users, administrators and groups to group "
          "(prefix: %s<group>, %s<admin>)", group_prefix, admin_prefix) },
        { "remove-user", value<std::vector<std::string>>(),
          "remove user from group" },
        { "remove-admin", value<std::vector<std::string>>(),
          "remove administrator from group" },
        { "remove-group", value<std::vector<std::string>>(),
          "remove group from group" },
        { "remove", value<std::vector<std::string>>()->multitoken(),
          elle::sprintf("remove users, administrators and groups from group "
                        "(prefix: %s<group>, %s<admin>)",
                        group_prefix, admin_prefix) },
        { "path,p", value<std::string>(), "a path within the volume" },
        fallback_option,
        verbose_option,
        fetch_option,
      },
    },
    {
      "register",
      "Register user's passport to the network",
      &register_,
      "--user USER --network NETWORK --path PATH_TO_MOUNTPOINT",
      {
        { "user,u", value<std::string>(), "user to register"},
        { "path,p", value<std::string>(), "path to mountpoint" },
        { "network,n", value<std::string>(), "name of the network"},
        { "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
          "if system xattrs are not suppported" },
        fetch_option,
      },
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
