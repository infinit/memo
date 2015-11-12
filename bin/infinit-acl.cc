#include <sys/types.h>

#ifdef INFINIT_LINUX
#include <attr/xattr.h>
#else
#include <sys/xattr.h>
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

using namespace boost::program_options;
options_description mode_options("Modes");

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
list_action(std::string const& path, bool verbose)
{
  if (verbose)
    std::cout << "processing " << path << std::endl;
  bool dir = boost::filesystem::is_directory(path);
  char buf[4096];
  int sz = getxattr(
    path.c_str(), "user.infinit.auth", buf, 4095 SXA_EXTRA SXA_EXTRA);
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
      int sz = getxattr(path.c_str(),
                        "user.infinit.auth.inherit",
                        buf,
                        4095 SXA_EXTRA SXA_EXTRA);
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
      using namespace elle::json;
      std::stringstream output;
      output << path << ":" << std::endl;
      if (dir_inherit)
        output << "  inherit: "
               << (dir_inherit.get() ? "yes" : "no") << std::endl;
      Json j = elle::json::read(ss);
      auto a = boost::any_cast<Array>(j);
      for (auto& li: a)
      {
        auto d = boost::any_cast<Object>(li);
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
           bool try_with_public_key)
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
        check(setxattr,
              path.c_str(),
              "user.infinit.auth.inherit",
              inherit ? "true" : "false",
              strlen("true") + (inherit ? 0 : 1),
              0 SXA_EXTRA);
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
      auto set = [path, mode] (std::string const& value) {
        check(setxattr,
              path.c_str(),
              ("user.infinit.auth." + mode).c_str(),
              value.c_str(),
              value.size(),
              0 SXA_EXTRA);
      };
      try
      {
        set(username);
      }
      catch (InvalidArgument const&)
      {
        if (!try_with_public_key)
          throw;
        try
        {
          auto user = ifnt.user_get(username);
          elle::Buffer buf;
          {
            elle::IOStream ios(buf.ostreambuf());
            elle::serialization::json::SerializerOut so(ios, false);
            so.serialize_forward(user.public_key);
          }
          set(buf.string());
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

static
void
list(variables_map const& args)
{
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  for (auto const& path: paths)
  {
    list_action(path, verbose);
    if (recursive)
      recursive_action(list_action, path, verbose);
  }
}

static
void
set(variables_map const& args)
{
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  auto users = mandatory<std::vector<std::string>>(args, "user", "user");
  std::vector<std::string> allowed_modes = {"r", "w", "rw", "none", ""};
  auto mode = mandatory(args, "mode");
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
  bool try_with_public_key = flag(args, "try-with-public-key");
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
    set_action(path, users, mode, inherit, disinherit, verbose,
               try_with_public_key);
    if (recursive)
    {
      recursive_action(
        set_action, path, users, mode, inherit, disinherit, verbose,
        try_with_public_key);
    }
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  Modes modes {
    {
      "list",
      "List current ACL",
      &list,
      "--path PATH",
      {
        { "path,p", value<std::vector<std::string>>(), "path" },
        { "recursive,R", bool_switch(), "list recursively" },
        { "verbose", bool_switch(), "verbose output" },
      },
    },
    {
      "set",
      "Set ACL",
      &set,
      "--path PATH [OPTIONS...]",
      {
        { "path,p", value<std::vector<std::string>>(), "path" },
        { "user", value<std::vector<std::string>>(), "user" },
        { "mode", value<std::string>(), "mode: r,w,rw,none" },
        { "enable-inherit,i", bool_switch(),
          "new files/folders inherit from their parent directory" },
        { "disable-inherit", bool_switch(),
          "new files/folders do not inherit from their parent directory" },
        { "recursive,R", bool_switch(), "list recursively" },
        { "try-with-public-key", bool_switch(), "try with the user public key" },
        { "verbose", bool_switch(), "verbose output" },
      },
    },
  };
  return infinit::main("Infinit ACL utility", modes, argc, argv,
                       std::string("path"));
}
