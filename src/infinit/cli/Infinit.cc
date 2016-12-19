#include <infinit/cli/Infinit.hh>

#include <iostream>
#include <iterator>
#include <regex>
#include <type_traits>
#include <vector>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <elle/Exit.hh>
#include <elle/format/hexadecimal.hh>
#include <elle/printf.hh>
#include <elle/string/algorithm.hh>

#include <cryptography/hash.hh>

#include <das/Symbol.hh>
#include <das/cli.hh>

#include <infinit/utility.hh>

#include <infinit/cli/utility.hh>

ELLE_LOG_COMPONENT("infinit");

static
char const* argv_0 = nullptr;

namespace das
{
  namespace named
  {
    template <typename ... Default, typename ... Args, typename ... NewArgs>
    Prototype<
      DefaultStore<NewArgs ..., Args...>,
      typename std::remove_cv_reference<NewArgs>::type..., Args...>
    extend(Prototype<DefaultStore<Default...>, Args...> const& p,
           NewArgs&& ... args)
    {
      return Prototype<
        DefaultStore<NewArgs ..., Args...>,
        typename std::remove_cv_reference<NewArgs>::type..., Args...>
        (DefaultStore<NewArgs ..., Args...>(std::forward<NewArgs>(args)...,
                                            p.defaults));
    }
  }
}

namespace infinit
{
  namespace cli
  {
    class CLIError
      : public das::cli::Error
    {
      using das::cli::Error::Error;
    };

    static das::cli::Options options = {
      {"help", {'h', "show this help message"}},
      {"version", {'v', "show software version"}},
    };

    Infinit::Infinit(infinit::Infinit& infinit)
      : InfinitCallable(
        das::bind_method(*this, cli::call),
        cli::help = false,
        cli::version = false)
      , block(*this)
      , credentials(*this)
      , device(*this)
      , silo(*this)
      , user(*this)
      , _infinit(infinit)
      , _as()
      , _script(false)
    {}

    std::string
    Infinit::default_user_name()
    {
      static auto const res =
        elle::os::getenv("INFINIT_USER", elle::system::username());
      return res;
    }

    infinit::User
    Infinit::default_user()
    {
      return this->infinit().user_get(Infinit::default_user_name());
    }

    infinit::User
    Infinit::as_user()
    {
      return this->infinit().user_get(this->_as.get());
    }

    namespace
    {
      template <typename Symbol>
      struct help_object
      {
        using type = bool;
        static
        bool
        value(std::ostream& s)
        {
          elle::fprintf(s, "  %s\n", Symbol::name());
          return true;
        }
      };
    }

    void
    Infinit::help(std::ostream& s) const
    {
      usage(s, "[OBJECT|--version|--help]");
      s << "Infinit decentralized storage platform.\n"
        << "\n"
        << "Object types:\n";
      infinit::cli::Infinit::Entities::map<help_object>::value(s);
      s << "\n"
        << "Options:\n";
      das::cli::help(*this, s, options);
    }

    void
    Infinit::call(bool help, bool version) const
    {
      if (help)
        this->help(std::cout);
      else if (version)
        std::cout << infinit::version_describe() << std::endl;
      else
      {
        this->help(std::cerr);
        throw elle::Exit(1);
      }
    }

    bool
    is_version_supported(elle::Version const& version)
    {
      auto const& deps = infinit::serialization_tag::dependencies;
      return std::find_if(deps.begin(), deps.end(),
                          [version] (auto const& kv) -> bool
                          {
                            return kv.first.major() == version.major() &&
                              kv.first.minor() == version.minor();
                          }) != deps.end();
    }

    void
    ensure_version_is_supported(elle::Version const& version)
    {
      if (!is_version_supported(version))
      {
        auto const& deps = infinit::serialization_tag::dependencies;
        std::vector<elle::Version> supported_versions(deps.size());
        std::transform(
          deps.begin(), deps.end(), supported_versions.begin(),
          [] (auto const& kv)
          {
            return elle::Version{kv.first.major(), kv.first.minor(), 0};
          });
        std::sort(supported_versions.begin(), supported_versions.end());
        supported_versions.erase(
          std::unique(supported_versions.begin(), supported_versions.end()),
          supported_versions.end());
        // Find the max value for the major.
        std::vector<elle::Version> versions_for_major;
        std::copy_if(supported_versions.begin(), supported_versions.end(),
                     std::back_inserter(versions_for_major),
                     [&] (elle::Version const& c)
                     {
                       return c.major() == version.major();
                     });
        if (versions_for_major.size() > 0)
        {
          if (version < versions_for_major.front())
            elle::err("Minimum compatibility version for major version %s is %s",
                      (int) version.major(), supported_versions.front());
          else if (version > versions_for_major.back())
            elle::err("Maximum compatibility version for major version %s is %s",
                      (int) version.major(), versions_for_major.back());
        }
        elle::err("Unknown compatibility version, try one of %s",
                  elle::join(supported_versions.begin(),
                             supported_versions.end(),
                             ", "));
      }
    }

    template <typename Symbol, typename ObjectSymbol>
    struct mode
    {
      using Object = typename ObjectSymbol::template attr_type<Infinit>::type;
      using type = bool;
      static
      bool
      value(infinit::cli::Infinit& infinit,
            Object& o,
            std::vector<std::string>& args,
            bool& found)
      {
        if (!found && Symbol::name() == args[0])
        {
          found = true;
          args.erase(args.begin());
          auto& mode = Symbol::attr_get(o);
          auto options = mode.options;
          auto f = das::named::extend(
            mode.prototype(),
            help = false,
            cli::compatibility_version = boost::none,
            script = false,
            as = infinit.default_user_name());
          auto show_help = [&] (std::ostream& s)
            {
              auto vars = VarMap{
                {"action", elle::sprintf("to %s", Symbol::name())},
                {"hub", beyond(true)},
                {"type", ObjectSymbol::name()},
              };
              Infinit::usage(s, elle::sprintf("%s %s [OPTIONS]",
                                              ObjectSymbol::name(),
                                              Symbol::name()));
              s << vars.expand(mode.help) << "\n\nOptions:\n";
              {
                std::stringstream buffer;
                das::cli::help(f, buffer, options);
                s << vars.expand(buffer.str());
              }
            };
          try
          {
            das::cli::call(
              f,
              [&] (bool help,
                   boost::optional<elle::Version> const& compatibility_version,
                   bool script,
                   std::string as,
                   auto&& ... args)
              {
                infinit._as = as;
                infinit._script = script;
                if (compatibility_version)
                {
                  ensure_version_is_supported(*compatibility_version);
                  infinit._compatibility_version =
                    std::move(compatibility_version);
                }
                if (help)
                  show_help(std::cout);
                else
                  mode.function()(std::forward<decltype(args)>(args)...);
              },
              args,
              options);
          }
          catch (das::cli::Error const& e)
          {
            std::stringstream s;
            s << e.what() << "\n\n";
            show_help(s);
            // object.help(s);
            // Discard trailing newline
            auto msg = s.str();
            msg = msg.substr(0, msg.size() - 1);
            throw CLIError(msg);
          }
          return true;
        }
        else
          return false;
      }
    };

    template <typename Symbol>
    struct object
    {
      using type = bool;
      static
      bool
      value(infinit::cli::Infinit& infinit,
            std::vector<std::string>& args,
            bool& found)
      {
        using Object =
          typename Symbol::template attr_type<infinit::cli::Infinit>::type;
        auto& object = Symbol::attr_get(infinit);
        if (!found && Symbol::name() == args[0])
        {
          found = true;
          args.erase(args.begin());
          try
          {
            if (args.empty() || das::cli::is_option(args[0], object.options()))
                das::cli::call(object, args, object.options());
            else
            {
              bool found = false;
              Object::Modes::template map<mode, Symbol>::value(
                infinit, object, args, found);
              if (!found)
                throw das::cli::Error(
                  elle::sprintf("unknown mode for object %s: %s",
                                Symbol::name(), args[0]));
            }
            return true;
          }
          catch (CLIError const& e)
          {
            throw;
          }
          catch (das::cli::Error const& e)
          {
            std::stringstream s;
            s << e.what() << "\n\n";
            object.help(s);
            // Discard trailing newline
            auto msg = s.str();
            msg = msg.substr(0, msg.size() - 1);
            throw CLIError(msg);
          }
        }
        else
          return false;
      }
    };

    /// From an old executable name (e.g. `infinit-users` or
    /// `infinit-users.exe`), extract the entity (e.g., `users`).
    std::string
    entity(std::string const& argv0)
    {
      // Mandatory prefix.
      static auto const prefix = std::string("infinit-");
      if (!boost::algorithm::starts_with(argv0, prefix))
        elle::err("unrecognized infinit executable name: %s", argv0);
      auto res = argv0.substr(prefix.size());
      // Possible suffix.
      static auto const suffix = std::string(".exe");
      if (boost::algorithm::ends_with(res, suffix))
        res.resize(res.size() - suffix.size());
      // Renamed entities.
      if (res == "storage")
        res = "silo";
      return res;
    }

    void
    main(std::vector<std::string>& args)
    {
      if (boost::filesystem::path(args[0]).filename() == "infinit")
        args.erase(args.begin());
      else
      {
        // The name of the command typed by the user, say `infinit-users`.
        auto prev = boost::filesystem::path(args[0]).filename().string();
        // The corresponding entity, say `users`
        args[0] = entity(prev);
        if (args.size() > 1 && das::cli::is_option(args[1]))
          if (args[1] == "-v" || args[1] == "--version")
            args.erase(args.begin());
          else if (args[1] != "-h" && args[1] != "--help")
            // This is the mode.  We no longer require a leading `--`.
            args[1] = args[1].substr(2);
        ELLE_WARN("%s is deprecated, please run: infinit %s",
                  prev, boost::algorithm::join(args, " "));
      }
      auto infinit = infinit::Infinit{};
      auto cli = Infinit(infinit);
      if (args.empty() || das::cli::is_option(args[0], options))
        das::cli::call(cli, args, options);
      else
      {
        bool found = false;
        infinit::cli::Infinit::Entities::map<object>::value(cli, args, found);
        if (!found)
        {
          std::stringstream s;
          elle::fprintf(s, "unknown object type: %s\n\n", args[0]);
          cli.help(s);
          throw das::cli::Error(s.str());
        }
      }
    }

    /*--------.
    | Helpers |
    `--------*/

    void
    Infinit::usage(std::ostream& s, std::string const& usage)
    {
      s << "Usage: ";
      if (argv_0)
        s << argv_0 << " ";
      else
        s << "infinit ";
      s << usage << std::endl;
    }

    using Input =
      std::unique_ptr<std::istream, std::function<void (std::istream*)>>;
    Input
    Infinit::get_input(boost::optional<std::string> const& path)
    {
      if (path && path.get() != "-")
      {
        auto res = Input(new std::ifstream(path.get()),
                         [] (std::istream* p) { delete p; });
        if (!res->good())
          elle::err("unable to open \"%s\" for reading", path.get());
        return res;
      }
      else
        return Input(&std::cin,
                     [] (std::istream*) {});
    }

    using Output =
      std::unique_ptr<std::ostream, std::function<void (std::ostream*)>>;
    Output
    Infinit::get_output(boost::optional<std::string> path, bool stdout_def)
    {
      if (path)
        if (path.get() != "-")
        {
          auto res = Output(new std::ofstream(path.get()),
                            [] (std::ostream* p) { delete p; });
          if (!res->good())
            elle::err("unable to open \"%s\" for writing", path.get());
          return res;
        }
        else
          return Output(&std::cout, [] (std::ostream*) {});
      else
        if (stdout_def)
          return Output(&std::cout, [] (std::ostream*) {});
        else
          return nullptr;
    }

    boost::filesystem::path
    Infinit::avatar_path() const
    {
      auto root = xdg_cache_home() / "avatars";
      create_directories(root);
      return root;
    }

    boost::optional<boost::filesystem::path>
    Infinit::avatar_path(std::string const& name) const
    {
      auto path = this->avatar_path() / name;
      if (exists(path))
        return path;
      else
        return {};
    }


    /*-------.
    | Report |
    `-------*/

    void
    Infinit::report(std::string const& msg)
    {
      if (!this->script())
        elle::printf("%s%s.\n", (char)toupper(msg[0]), msg.substr(1));
    }

    void
    Infinit::report_action(std::string const& action,
                           std::string const& type,
                           std::string const& name,
                           std::string const& where)
    {
      if (where.empty())
        report("%s %s \"\%s\"", action, type, name);
      else
        report("%s %s %s \"\%s\"", where, action, type, name);
    }

    void
    Infinit::report_created(std::string const& type, std::string const& name)
    {
      report_action("created", type, name, "locally");
    }

    void
    Infinit::report_updated(std::string const& type, std::string const& name)
    {
      report_action("updated", type, name, "locally");
    }

    void
    Infinit::report_imported(std::string const& type, std::string const& name)
    {
      report_action("imported", type, name);
    }

    void
    Infinit::report_saved(std::string const& type, std::string const& name)
    {
      report_action("saved", type, name);
    }

    void
    Infinit::report_action_output(std::ostream& output,
                                  std::string const& action,
                                  std::string const& type,
                                  std::string const& name)
    {
      if (&output != &std::cout)
        report_action(action, type, name);
    }

    void
    Infinit::report_exported(std::ostream& output,
                             std::string const& type,
                             std::string const& name)
    {
      report_action_output(output, "exported", type, name);
    }

    /*---------.
    | Password |
    `---------*/

    namespace
    {
      void
      echo_mode(bool enable)
      {
#if defined(INFINIT_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdin, &mode);
        if (!enable)
          mode &= ~ENABLE_ECHO_INPUT;
        else
          mode |= ENABLE_ECHO_INPUT;
        SetConsoleMode(hStdin, mode );
#else
        struct termios tty;
        tcgetattr(STDIN_FILENO, &tty);
        if(!enable)
          tty.c_lflag &= ~ECHO;
        else
          tty.c_lflag |= ECHO;
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
      }

      /// Read something secret on std::in.
      std::string
      _read_secret(std::string const& prompt)
      {
        std::string res;
        {
          elle::SafeFinally restore_echo([] { echo_mode(true); });
          echo_mode(false);
          std::cout << prompt << ": ";
          std::cout.flush();
          std::getline(std::cin, res);
        }
        std::cout << std::endl;
        return res;
      }

    }

    std::string
    Infinit::read_secret(std::string const& prompt,
                         std::string const& regex)
    {
      auto re = std::regex{regex.empty() ? ".*" : regex};
      while (true)
      {
        auto res = _read_secret(prompt);
        if (std::regex_match(res, re))
          return res;
        std::cerr << "Invalid \"" << prompt << "\", try again...\n";
      }
    }

    std::string
    Infinit::read_passphrase()
    {
      return _read_secret("Passphrase");
    }

    std::string
    Infinit::read_password()
    {
      return _read_secret("Password");
    }

    std::string
    Infinit::hash_password(std::string const& password_,
                           std::string salt)
    {
      auto password = password_ + salt;
      return elle::format::hexadecimal::encode(
        infinit::cryptography::hash(
          password, infinit::cryptography::Oneway::sha256).string()
        );
      return password;
    };

    static
    std::string const
    _hub_salt = "@a.Fl$4'x!";

    std::string
    Infinit::hub_password_hash(std::string const& password)
    {
      return Infinit::hash_password(password, _hub_salt);
    }
  }
}

int
main(int argc, char** argv)
{
  argv_0 = argv[0];
  try
  {
    std::vector<std::string> args;
    std::copy(argv, argv + argc, std::back_inserter(args));
    reactor::Scheduler s;
    reactor::Thread main(s, "main", [&] { infinit::cli::main(args); });
    s.run();
  }
  catch (das::cli::Error const& e)
  {
    elle::fprintf(std::cerr, "%s: command line error: %s\n", argv[0], e.what());
    return 2;
  }
  catch (elle::Error const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    if (elle::os::inenv("INFINIT_BACKTRACE"))
      elle::fprintf(std::cerr, "%s\n", e.backtrace());
    return 1;
  }
  catch (elle::Exit const& e)
  {
    return e.return_code();
  }
}
