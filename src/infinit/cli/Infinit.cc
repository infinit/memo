#include <infinit/cli/Infinit.hh>
#include <infinit/cli/Infinit-template.hxx>

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

#include <cryptography/hash.hh>

#include <das/Symbol.hh>
#include <das/cli.hh>

#include <infinit/utility.hh>

#include <infinit/cli/utility.hh>

ELLE_LOG_COMPONENT("infinit");

namespace bfs = boost::filesystem;

namespace
{
  /// argv[0], for error messages.
  char const* argv_0 = "infinit";
}

namespace infinit
{
  namespace cli
  {
    namespace
    {
      void
      install_signal_handlers(Infinit& cli)
      {
        auto main_thread = reactor::scheduler().current();
        assert(main_thread);
        if (!getenv("INFINIT_DISABLE_SIGNAL_HANDLER"))
        {
          static const auto signals = {SIGINT, SIGTERM
#ifndef INFINIT_WINDOWS
                                       , SIGQUIT
#endif
          };
          for (auto signal: signals)
          {
#ifndef INFINIT_WINDOWS
            ELLE_DEBUG("set signal handler for %s", strsignal(signal));
#endif
            reactor::scheduler().signal_handle(
              signal,
              [main_thread, &cli]
              {
                main_thread->terminate();
                cli.killed();
              });
          }
        }
      }
    }



    Infinit::Infinit(infinit::Infinit& infinit)
      : InfinitCallable(
        das::bind_method(*this, cli::call),
        cli::help = false,
        cli::version = false)
      , _infinit(infinit)
      , _as()
      , _script(false)
    {
      install_signal_handlers(*this);
    }

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
      das::cli::Options options = {
        {"help", {'h', "show this help message"}},
        {"version", {'v', "show software version"}},
      };

      template <typename Symbol>
      struct help_object
      {
        using type = bool;
        static
        bool
        value(std::ostream& s)
        {
          elle::fprintf(
            s, "  %s\n", das::cli::option_name_from_c(Symbol::name()));
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
      infinit::cli::Infinit::Objects::map<help_object>::value(s);
      s << "\n"
        << "Options:\n"
        << das::cli::help(*this, options);
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
        elle::fprintf(std::cerr,
                      "Try '%s --help' for more information.\n",
                      argv_0);
        throw elle::Exit(1);
      }
    }

    namespace
    {
      /// Filename, with possible `.exe` suffix removed.
      std::string
      program_name(std::string const& argv0)
      {
        // Possible suffix.
        auto res = bfs::path(argv0).filename().string();
        static auto const suffix = std::string(".exe");
        if (boost::algorithm::ends_with(res, suffix))
          res.resize(res.size() - suffix.size());
        return res;
      }

      /// From an old executable name (e.g. `infinit-users`), extract
      /// the object (e.g., `users`).
      std::string
      object_from(std::string const& argv0)
      {
        // Mandatory prefix.
        static auto const prefix = std::string("infinit-");
        if (!boost::algorithm::starts_with(argv0, prefix))
          elle::err("unrecognized infinit executable name: %s", argv0);
        auto res = argv0.substr(prefix.size());
        // Renamed objects.
        if (res == "storage")
          res = "silo";
        return res;
      }

      /// Return true if we found (and ran) the command.
      bool
      run_command(Infinit& cli, std::vector<std::string>& args)
      {
        bool res = false;
        infinit::cli::Infinit::Objects::map<object>::value(cli, args, res);
        return res;
      }

      void
      main(std::vector<std::string>& args)
      {
        // The name of the command typed by the user, say `infinit-users`.
        auto prog = program_name(args[0]);
        if (prog == "infinit")
          args.erase(args.begin());
        else
        {
          // The corresponding object, say `users`.
          args[0] = object_from(prog);
          if (args.size() > 1 && das::cli::is_option(args[1]))
            if (args[1] == "-v" || args[1] == "--version")
              args.erase(args.begin());
            else if (args[1] != "-h" && args[1] != "--help")
              // This is the mode.  We no longer require a leading `--`.
              args[1] = args[1].substr(2);
          ELLE_WARN("%s is deprecated, please run: infinit %s",
                    prog, boost::algorithm::join(args, " "));
        }
        auto infinit = infinit::Infinit{};
        auto cli = Infinit(infinit);
        if (args.empty() || das::cli::is_option(args[0], options))
          das::cli::call(cli, args, options);
        else if (!run_command(cli, args))
          throw das::cli::Error
            (elle::sprintf("unknown object type: %s\n"
                           "Try '%s --help' for more information.",
                           args[0], argv_0));
      }
    }

    /*--------.
    | Helpers |
    `--------*/

    void
    Infinit::usage(std::ostream& s, std::string const& usage)
    {
      s << "Usage: " << argv_0 << " " << usage << std::endl;
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

    bfs::path
    Infinit::avatar_path() const
    {
      auto root = xdg_cache_home() / "avatars";
      create_directories(root);
      return root;
    }

    boost::optional<bfs::path>
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
      {
        elle::printf("%s%s.\n", (char)toupper(msg[0]), msg.substr(1));
        std::cout.flush();
      }
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

    std::string
    Infinit::hub_password_hash(std::string const& password)
    {
      static auto const salt = std::string{"@a.Fl$4'x!"};
      return Infinit::hash_password(password, salt);
    }

    // This overload is required, otherwise Infinit is printed as a
    // das::Function, which then prints its function, which is a
    // BoundMethod<Infinit, call>, which prints its object, that is the Infinit,
    // which recurses indefinitely.
    void
    Infinit::print(std::ostream& o) const
    {
      elle::fprintf(
        o, "%s(%s)", elle::type_info(*this), this->_infinit);
    }

  }
}

int
main(int argc, char** argv)
{
  argv_0 = argv[0];
  try
  {
    auto args = std::vector<std::string>(argv, argv + argc);
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
  catch (bfs::filesystem_error const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    return 1;
  }
  catch (elle::Exit const& e)
  {
    return e.return_code();
  }
}
