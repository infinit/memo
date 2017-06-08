#include <infinit/cli/Infinit.hh>

#include <iostream>
#include <iterator>
#include <regex>
#include <type_traits>
#include <vector>

#include <boost/range/algorithm/transform.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <elle/Exit.hh>
#include <elle/format/hexadecimal.hh>
#include <elle/printf.hh>

#include <elle/cryptography/hash.hh>

#include <elle/das/Symbol.hh>
#include <elle/das/cli.hh>

#include <infinit/cli/Error.hh>
#include <infinit/cli/utility.hh>
#include <infinit/environ.hh>
#include <infinit/report-crash.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("cli");

#ifdef INFINIT_BINARY
# define BIN "infinit"
#else
# define BIN "memo"
#endif

namespace bfs = boost::filesystem;

namespace
{
  /// The path to `memo`.
  auto memo_exe = std::string(BIN);
  auto argv_0 = std::string(BIN);
}

namespace infinit
{
  namespace cli
  {
    namespace
    {
      void
      install_signal_handlers(Memo& cli)
      {
        auto main_thread = elle::reactor::scheduler().current();
        assert(main_thread);
        if (!elle::os::getenv("INFINIT_DISABLE_SIGNAL_HANDLER", false))
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
            elle::reactor::scheduler().signal_handle(
              signal,
              [main_thread, &cli]
              {
                main_thread->terminate();
                cli.killed();
              });
          }
        }
      }

      void
      check_broken_locale()
      {
#if defined INFINIT_LINUX
        // boost::filesystem uses the default locale, detect here if
        // it can't be instantiated.  Not required on OS X, see
        // boost/libs/filesystem/src/path.cpp:819.
        try
        {
          std::locale("");
        }
        catch (std::exception const& e)
        {
          ELLE_WARN("Something is wrong with your locale settings,"
                    " overriding: %s",
                    e.what());
          elle::os::setenv("LC_ALL", "C");
        }
#endif
      }
    }

    namespace
    {
      std::string
      pretty_object(std::string const& object)
      {
        if (object == "user")
          return "identity for user";
        else if (object == "network")
          return "network descriptor";
        else
          return object;
      }
    }

    /*----------.
    | Infinit.  |
    `----------*/

    Memo::Memo(infinit::Infinit& infinit)
      : MemoCallable(
        elle::das::bind_method(*this, cli::call),
        cli::help = false,
        cli::version = false)
      , _infinit(infinit)
      , _as()
      , _script(false)
    {
      install_signal_handlers(*this);
      this->_infinit.report_local_action().connect(
        [this] (std::string const& action,
                std::string const& type,
                std::string const& name)
        {
          this->report_action(action, pretty_object(type), name, "locally");
        });
      this->_infinit.report_remote_action().connect(
        [this] (std::string const& action,
                std::string const& type,
                std::string const& name)
        {
          this->report_action(action, pretty_object(type), name, "remotely");
        });
    }

    std::string
    Memo::default_user_name() const
    {
      static auto const res =
        elle::os::getenv("MEMO_USER", elle::system::username());
      return res;
    }

    infinit::User
    Memo::default_user()
    {
      return this->infinit().user_get(this->default_user_name());
    }

    infinit::User
    Memo::as_user()
    {
      return this->infinit().user_get(this->_as.get());
    }

    namespace
    {
      auto const options = elle::das::cli::Options
        {
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
            s, "  %s\n", elle::das::cli::option_name_from_c(Symbol::name()));
          return true;
        }
      };
    }

    void
    Memo::help(std::ostream& s) const
    {
      usage(s, "[OBJECT|--version|--help]");
      s << "memo key-value store.\n"
        << "\n"
        << "Object types:\n";
      infinit::cli::Memo::Objects::map<help_object>::value(s);
      s << "\n"
        << "Options:\n"
        << elle::das::cli::help(*this, options);
    }

    void
    Memo::call(bool help, bool version) const
    {
      if (help)
        this->help(std::cout);
      else if (version)
        std::cout << infinit::version_describe() << std::endl;
      else
        elle::err<CLIError>("missing object type");
    }

    namespace
    {
      /// Return true if we found (and ran) the command.
      bool
      run_command(Memo& cli, std::vector<std::string>& args)
      {
        cli.command_line(args);
        bool res = false;
        infinit::cli::Memo::Objects::map<mode_call, Memo>::value(
          cli, cli, args, res);
        return res;
      }

      void
      main_impl(std::vector<std::string>& args)
      {
        args.erase(args.begin());
        auto infinit = infinit::Infinit{};
        auto&& cli = Memo(infinit);
        if (args.empty() || elle::das::cli::is_option(args[0], options))
          elle::das::cli::call(cli, args, options);
        else if (!run_command(cli, args))
          elle::err<CLIError>("unknown object type: %s", args[0]);
      }

      void
      main(std::vector<std::string>& args)
      {
        auto report_thread = make_reporter_thread();
        check_broken_locale();
        check_environment();
        main_impl(args);
        if (report_thread)
          elle::reactor::wait(*report_thread);
      }
    }

    /*--------.
    | Helpers |
    `--------*/

    void
    Memo::usage(std::ostream& s, std::string const& usage)
    {
      s << "Usage: " << memo_exe << ' ' << usage << std::endl;
    }

    /// An input file, and its clean-up function.
    using Input =
      std::unique_ptr<std::istream, std::function<void (std::istream*)>>;

    Input
    Memo::get_input(boost::optional<std::string> const& path)
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
    Memo::get_output(boost::optional<std::string> path, bool stdout_def)
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

    boost::optional<bfs::path>
    Memo::avatar_path(std::string const& name) const
    {
      auto path = this->infinit()._avatar_path(name);
      if (exists(path))
        return path;
      else
        return {};
    }


    /*-------.
    | Report |
    `-------*/

    void
    Memo::report(std::string const& msg)
    {
      if (!this->script())
      {
        elle::fprintf(std::cout,
                      "%s%s.\n", (char)toupper(msg[0]), msg.substr(1));
        std::cout.flush();
      }
    }

    void
    Memo::report_action(std::string const& action,
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
    Memo::report_imported(std::string const& type, std::string const& name)
    {
      this->report_action("imported", type, name);
    }

    void
    Memo::report_action_output(std::ostream& output,
                                  std::string const& action,
                                  std::string const& type,
                                  std::string const& name)
    {
      if (&output != &std::cout)
        this->report_action(action, type, name);
    }

    void
    Memo::report_exported(std::ostream& output,
                             std::string const& type,
                             std::string const& name)
    {
      this->report_action_output(output, "exported", type, name);
    }

    /*---------.
    | Password |
    `---------*/

    namespace
    {
      void
      echo_mode(bool enable)
      {
#if defined INFINIT_WINDOWS
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
        if (enable)
          tty.c_lflag |= ECHO;
        else
          tty.c_lflag &= ~ECHO;
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
    Memo::read_secret(std::string const& prompt,
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
    Memo::read_passphrase()
    {
      return _read_secret("Passphrase");
    }

    std::string
    Memo::read_password()
    {
      return _read_secret("Password");
    }

    std::string
    Memo::hash_password(std::string const& password_,
                           std::string salt)
    {
      auto password = password_ + salt;
      return elle::format::hexadecimal::encode(
        elle::cryptography::hash(
          password, elle::cryptography::Oneway::sha256).string()
        );
    };

    std::string
    Memo::hub_password_hash(std::string const& password)
    {
      static auto const salt = std::string{"@a.Fl$4'x!"};
      return Memo::hash_password(password, salt);
    }

    // This overload is required, otherwise Infinit is printed as a
    // elle::das::Function, which then prints its function, which is a
    // BoundMethod<Infinit, call>, which prints its object, that is the Infinit,
    // which recurses indefinitely.
    void
    Memo::print(std::ostream& o) const
    {
      elle::fprintf(o, "%s(%s)", elle::type_info(*this), this->_infinit);
    }
  }
}

namespace
{
  int
  cli_error(std::string const& error, boost::optional<std::string> object = {})
  {
    elle::fprintf(std::cerr, "%s: command line error: %s\n", argv_0, error);
    auto const obj = object ? " " + *object : "";
    elle::fprintf(std::cerr,
                  "Try '%s%s --help' for more information.\n",
                  memo_exe, obj);
    return 2;
  }
}

int
main(int argc, char** argv)
{
  argv_0 = argv[0];
  memo_exe = (bfs::path(argv_0).parent_path() / BIN).string();
  if (boost::algorithm::ends_with(argv_0, ".exe"))
    memo_exe += ".exe";
  try
  {
    auto args = std::vector<std::string>(argv, argv + argc);
    elle::reactor::Scheduler s;
    elle::reactor::Thread main(s, "main", [&] { infinit::cli::main(args); });
    s.run();
  }
  catch (infinit::cli::CLIError const& e)
  {
    return cli_error(e.what(), e.object());
  }
  catch (elle::das::cli::Error const& e)
  {
    return cli_error(e.what());
  }
  catch (elle::Error const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    if (elle::os::getenv("INFINIT_BACKTRACE", false))
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
