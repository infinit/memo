
#include <sys/types.h>

#ifdef INFINIT_LINUX
#include <attr/xattr.h>
#else
#include <sys/xattr.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <elle/log.hh>

#ifdef INFINIT_MACOSX
  #define SXA_EXTRA ,0
#else
  #define SXA_EXTRA
#endif


bool recursive = false;
bool inherit = false;
bool dishinerit = false;
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
  if (list)
  {
    char buf[4096];
    int sz = getxattr(file.c_str(), "user.infinit.auth", buf, 4095 SXA_EXTRA SXA_EXTRA);
    if (sz < 0)
      perror(file.c_str());
    else
    {
      buf[sz] = 0;
      std::cout << file << " : " << buf << std::endl;
    }
  }
  using namespace boost::filesystem;
  bool dir = is_directory(file);
  if (inherit || dishinerit)
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
   ("recursive,R", bool_switch(&recursive), "apply operatior recursively")
   ("list,l", bool_switch(&list), "list current ACL")
   ("inherit,i", bool_switch(&inherit), "if set, new files/folder will inherit ACLs frome their parent directory")
   ("disinherit,d", bool_switch(&dishinerit), "Disable inheritance flag")
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
    if (vm.count("help") || show_help)
    {
      std::cout << "Usage: " << argv[0] << " [options] mode user path..." << std::endl;
      std::cout << options << std::endl;
      exit(0);
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