#include <sys/types.h>

#ifdef INFINIT_LINUX
# include <attr/xattr.h>
#elif defined(INFINIT_MACOSX)
# include <sys/xattr.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <elle/Exit.hh>
#include <elle/Error.hh>
#include <elle/log.hh>
#include <elle/json/json.hh>

ELLE_LOG_COMPONENT("infinit-acl");

#include <main.hh>

#ifdef INFINIT_MACOSX
# define SXA_EXTRA ,0
#else
# define SXA_EXTRA
#endif

infinit::Infinit ifnt;

static
boost::filesystem::path
file_xattrs_dir(std::string const& file)
{
  boost::filesystem::path p(file);
  auto filename = p.filename();
  auto dir = p.parent_path();
  boost::filesystem::path res;
  // dir might be outside the filesystem, so dont go below file if its a
  // directory
  if (boost::filesystem::is_directory(file))
    res = p / "$xattrs..";
  else
    res = dir / ("$xattrs." + filename.string());
  boost::system::error_code erc;
  boost::filesystem::create_directory(res, erc);
  return res;
}

static
int
port_getxattr(std::string const& file,
              std::string const& key,
              char* val, int val_size,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
  int res = -1;
  res = getxattr(file.c_str(), key.c_str(), val, val_size SXA_EXTRA SXA_EXTRA);
  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ifstream ifs(attr_dir / key);
  ifs.read(val, val_size);
  return ifs.gcount();
}

static
int
port_setxattr(std::string const& file,
              std::string const& key,
              std::string const& value,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
  int res = setxattr(
    file.c_str(), key.c_str(), value.data(), value.size(), 0 SXA_EXTRA);
  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ofstream ofs(attr_dir / key);
  ofs.write(value.data(), value.size());
  return 0;
}

class InvalidArgument
  : public elle::Error
{
public:
  InvalidArgument(std::string const& error)
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
  for (; it != bfs::recursive_directory_iterator(); ++it)
    action(it->path().string(), args...);
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
        output << "  inherit: "
               << (dir_inherit.get() ? "yes" : "no")
               << std::endl;
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
           bool inherit,
           bool disinherit,
           bool verbose,
           bool fallback_xattrs)
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
      catch (elle::Error const& error)
      {
        ELLE_ERR("setattr (inherit) on %s failed: %s", path,
                 elle::exception_string());
      }
    }
  }
  if (!mode.empty())
  {
    for (auto& username: users)
    {
      auto set_attribute =
        [path, mode, fallback_xattrs] (std::string const& value)
        {
          check(port_setxattr, path, ("user.infinit.auth." + mode), value,
                fallback_xattrs);
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
          auto user = ifnt.user_get(username);
          elle::Buffer buf;
          {
            elle::IOStream ios(buf.ostreambuf());
            elle::serialization::json::SerializerOut so(ios, false);
            so.serialize_forward(user.public_key);
          }
          set_attribute(buf.string());
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
  auto users_ = optional<std::vector<std::string>>(args, "user");
  auto users = users_ ? users_.get() : std::vector<std::string>();
  std::vector<std::string> allowed_modes = {"r", "w", "rw", "none", ""};
  auto mode_ = optional(args, "mode");
  auto mode = mode_ ? mode_.get() : "";
  auto it = std::find(allowed_modes.begin(), allowed_modes.end(), mode);
  if (it == allowed_modes.end())
  {
    throw CommandLineError(
      elle::sprintf("mode must be one of: %s", allowed_modes));
  }
  if (!mode.empty() && users.empty())
    throw CommandLineError("must specify user when setting mode");
  bool inherit = flag(args, "enable-inherit");
  bool disinherit = flag(args, "disable-inherit");
  if (inherit && disinherit)
  {
    throw CommandLineError(
      "either inherit or disable-inherit can be set, not both");
  }
  if (!inherit && !disinherit && mode.empty())
    throw CommandLineError("no operation specified");
  std::vector<std::string> modes_map = {"setr", "setw", "setrw", "clear", ""};
  mode = modes_map[it - allowed_modes.begin()];
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  bool fallback = flag(args, "fallback-xattrs");
  // Don't do any operations before checking paths.
  for (auto const& path: paths)
  {
    if ((inherit || disinherit) &&
      !recursive && !boost::filesystem::is_directory(path))
    {
      throw CommandLineError(elle::sprintf(
        "%s is not a directory, cannot %s inherit",
        path, inherit ? "enable" : "disable"));
    }
  }
  for (auto const& path: paths)
  {
    set_action(path, users, mode, inherit, disinherit, verbose, fallback);
    if (recursive)
    {
      recursive_action(
        set_action, path, users, mode, inherit, disinherit, verbose, fallback);
    }
  }
}

COMMAND(group)
{
  auto g = mandatory<std::string>(args, "name", "group name");
  auto add = optional<std::vector<std::string>>(args, "add");
  auto rem = optional<std::vector<std::string>>(args, "remove");
  auto adm_add = optional<std::vector<std::string>>(args, "admin-add");
  auto adm_rem = optional<std::vector<std::string>>(args, "admin-remove");
  bool create = flag(args, "create");
  bool del = flag(args, "delete");
  bool fallback = flag(args, "fallback-xattrs");
  bool list = flag(args, "show");
  std::string path = mandatory<std::string>(args, "path", "path to filesystem");
  if (create)
    check(port_setxattr, path, "user.infinit.group.create", g, fallback);
  if (del)
    check(port_setxattr, path, "user.infinit.group.delete", g, fallback);
  if (add) for (auto const& u: *add)
    check(port_setxattr, path, "user.infinit.group.add", g + ":" + u , fallback);
  if (rem) for (auto const& u: *rem)
    check(port_setxattr, path, "user.infinit.group.remove", g + ":" + u , fallback);
  if (adm_add) for (auto const& u: *adm_add)
    check(port_setxattr, path, "user.infinit.group.addadmin", g + ":" + u , fallback);
  if (adm_rem) for (auto const& u: *adm_rem)
    check(port_setxattr, path, "user.infinit.group.removeadmin", g + ":" + u , fallback);
  if (list)
  {
    char res[16384];
    int sz = port_getxattr(path, "user.infinit.group.list." + g, res, 16384, fallback);
    if (sz >=0)
    {
      res[sz] = 0;
      std::cout << res << std::endl;
    }
    else
      throw std::runtime_error("group query failure");
  }
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  program = argv[0];
  Modes modes {
    {
      "list",
      "List current ACL",
      &list,
      "--path PATHS",
      {
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "recursive,R", bool_switch(), "list recursively" },
        { "verbose", bool_switch(), "verbose output" },
        { "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
          "if system xattrs are not suppported" },
      },
    },
    {
      "set",
      "Set ACL",
      &set,
      "--path PATHS [--user USERS] [OPTIONS...]",
      {
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "user,u", value<std::vector<std::string>>(), "users" },
        { "mode,m", value<std::string>(), "access mode: r,w,rw,none" },
        { "enable-inherit,i", bool_switch(),
          "new files/folders inherit from their parent directory" },
        { "disable-inherit", bool_switch(),
          "new files/folders do not inherit from their parent directory" },
        { "recursive,R", bool_switch(), "apply recursively" },
        { "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
          "if system xattrs are not suppported" },
        { "verbose", bool_switch(), "verbose output" },
      },
    },
    {
      "group",
      "Group control",
      &group,
      "[--user USERS]",
      {
        { "name,n", value<std::string>(), "group name"},
        { "show,s", bool_switch(), "list group users and admins"},
        { "create,c", bool_switch(), "create the group"},
        { "delete,d", bool_switch(), "delete the group"},
        { "add,a", value<std::vector<std::string>>(), "users to add to group" },
        { "remove,r", value<std::vector<std::string>>(), "users to remove from group" },
        { "admin-add,A", value<std::vector<std::string>>(), "admins to add to group" },
        { "admin-remove,R", value<std::vector<std::string>>(), "admins to remove from group" },
        { "fallback-xattrs", bool_switch(), "fallback to creating xattrs "
          "folder if system xattrs are not suppported" },
        { "verbose", bool_switch(), "verbose output" },
        { "path,p", value<std::string>(), "path" },
      },
    }
  };
  return infinit::main("Infinit access control list utility", modes, argc, argv,
                       std::string("path"));

}
