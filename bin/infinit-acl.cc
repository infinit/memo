
#include <sys/types.h>

#ifdef INFINIT_LINUX
#include <attr/xattr.h>
#else
#include <sys/xattr.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <elle/Exit.hh>
#include <elle/log.hh>
#include <elle/json/json.hh>

#ifdef INFINIT_MACOSX
  #define SXA_EXTRA ,0
#else
  #define SXA_EXTRA
#endif

#include <infinit/version.hh>
bool recursive = false;
bool inherit = false;
bool disinherit = false;
bool list = false;
bool verbose = false;
std::vector<std::string> path;
std::vector<std::string> user;
std::string mode;

template<typename F, typename... ARGS>
void check(F func, std::string const& file, ARGS... args)
{
  int res = func(args...);
  if (res < 0)
    perror(file.c_str());
}

void apply(std::string const& file, bool from_rec)
{
  if (verbose)
    std::cout << "processing " << file << std::endl;
  using namespace boost::filesystem;
  bool dir = is_directory(file);
  if (list)
  {
    char buf[4096];
    int sz = getxattr(file.c_str(), "user.infinit.auth", buf, 4095 SXA_EXTRA SXA_EXTRA);
    if (sz < 0)
      perror(file.c_str());
    else
    {
      buf[sz] = 0;
      std::stringstream ss;
      ss.str(buf);
      boost::optional<bool> dir_inherit;
      if (dir)
      {
        int sz = getxattr(file.c_str(), "user.infinit.auth.inherit", buf, 4095 SXA_EXTRA SXA_EXTRA);
        if (sz < 0)
          perror(file.c_str());
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
        output << file << ":" << std::endl;
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
          const char* mode = w ? (r? "rw" : "w") : (r? "r" : "none");
          output << "\t" << n << ": " << mode << std::endl;
        }
        std::cout << output.str() << std::endl;
      }
      catch (std::exception const& e)
      {
        std::cout << file << " : " << buf << std::endl;
      }
    }
  }

  if (inherit || disinherit)
  {
    if (!dir)
    {
      if (!from_rec)
        std::cerr << "error: " << file << " is not a directory." << std::endl;
    }
    else
      check(setxattr, file, file.c_str(), "user.infinit.auth.inherit",
            inherit? "true" : "false", strlen("true") + (inherit?0:1), 0 SXA_EXTRA);
  }
  if (!mode.empty())
    for (auto& u: user)
    {
      check(setxattr, file, file.c_str(), ("user.infinit.auth." + mode).c_str(),
        u.c_str(), u.size(), 0 SXA_EXTRA);
    }
}

void apply_rec(std::string const& file)
{
  using namespace boost::filesystem;
  apply(file, false);
  boost::system::error_code erc;
  recursive_directory_iterator it(file, erc);
  if (erc)
  {
    std::cerr << file << ":" << erc.message() << std::endl;
    return;
  }
  for(; it != recursive_directory_iterator(); ++it)
  {
    apply(it->path().string(), true);
  }
}

int main(int argc, char** argv)
{
  using namespace boost::program_options;
  options_description options("options");
  options.add_options()
   ("help,h", "display the help")
   ("version,v", "display version")
   ("recursive,R", bool_switch(&recursive), "apply operation recursively")
   ("list,l", bool_switch(&list), "list current ACL")
   ("inherit,i", bool_switch(&inherit), "if set, new files/folder will inherit ACLs frome their parent directory")
   ("disinherit,d", bool_switch(&disinherit), "Disable inheritance flag")
   ("verbose,v", bool_switch(&verbose), "verbose output")
   ("path,p", value<std::vector<std::string> >(&path), "path")
   ("mode,m", value<std::string>(&mode), "mode {r, w, rw, none}")
   ("user,u", value<std::vector<std::string>>(&user), "user")
    ;
    positional_options_description pod;
    pod.add("path", -1);

    variables_map vm;
    store(command_line_parser(argc, argv).
      options(options).positional(pod).run(), vm);
    notify(vm);
    bool show_help = false;
    std::vector<std::string> modes = {"r", "w", "rw", "none", ""};
    auto it = std::find(modes.begin(), modes.end(), mode);
    if (it == modes.end())
    {
      std::cout << "mode must be one of 'r', 'w', 'rw' or 'none'." << std::endl;
      show_help = true;
    }
    if (mode.empty() != user.empty())
    {
      std::cout << "missing argument " << (mode.empty() ? "mode": "user") << std::endl;
      show_help = true;
    }
    if (path.empty())
    {
      std::cout << "Missing path argument" << std::endl;
      show_help = true;
    }
    if (mode.empty() && !inherit && !disinherit && !list)
    {
      std::cout << "No operation specified, set one of -i, -d, -m, -l" << std::endl;
      show_help = true;
    }
    if (vm.count("help") || show_help)
    {
      std::cout << "Usage: " << argv[0] << " [options] path..." << std::endl;
      std::cout << options << std::endl;
      exit(0);
    }
    if (vm.count("version"))
    {
      std::cout << INFINIT_VERSION << std::endl;
      throw elle::Exit(0);
    }
    std::vector<std::string> modes_map = {"setr", "setw", "setrw", "clear", ""};
    mode = modes_map[it - modes.begin()];
    for (auto& f: path)
    {
      if (recursive)
        apply_rec(f);
      else
        apply(f, false);
    }
}
