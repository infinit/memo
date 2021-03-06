#include <memo/cli/Memo.hh>

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

#include <memo/cli/Error.hh>
#include <memo/cli/utility.hh>
#include <memo/crash-report.hh>
#include <memo/environ.hh>
#include <memo/log.hh>
#include <memo/utility.hh>

ELLE_LOG_COMPONENT("cli");

namespace bfs = boost::filesystem;
using namespace std::literals;

namespace memo
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
        if (memo::getenv("SIGNAL_HANDLER", true))
        {
          static const auto signals = {SIGINT, SIGTERM
#ifndef ELLE_WINDOWS
                                       , SIGQUIT
#endif
          };
          for (auto signal: signals)
          {
#ifndef ELLE_WINDOWS
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
        else if (object == "kvs")
          return "kvs descriptor";
        else
          return object;
      }
    }

    /*-------.
    | Memo.  |
    `-------*/

    Memo::Memo(memo::Memo& memo)
      : MemoCallable(
        elle::das::bind_method(*this, cli::call),
        cli::help = false,
        cli::version = false)
      , _memo(memo)
      , _as()
      , _script(false)
    {
      install_signal_handlers(*this);
      this->backend().report_local_action().connect(
        [this] (std::string const& action,
                std::string const& type,
                std::string const& name)
        {
          this->report_action(action, pretty_object(type), name, "locally");
        });
      this->backend().report_remote_action().connect(
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
      static auto const res = memo::getenv("USER", elle::system::username());
      return res;
    }

    memo::User
    Memo::default_user()
    {
      return this->backend().user_get(this->default_user_name());
    }

    memo::User
    Memo::as_user()
    {
      return this->backend().user_get(this->_as.get());
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
      memo::cli::Memo::Objects::map<help_object>::value(s);
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
        std::cout << memo::version_describe() << std::endl;
      else
        elle::err<CLIError>("missing object type");
    }


    /*--------.
    | Helpers |
    `--------*/

    void
    Memo::usage(std::ostream& s, std::string const& usage)
    {
      s << "Usage: memo " << usage << std::endl;
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
      auto path = this->backend()._avatar_path(name);
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
#if defined ELLE_WINDOWS
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

    // This overload is required, otherwise Memo is printed as a
    // elle::das::Function, which then prints its function, which is a
    // BoundMethod<Memo, call>, which prints its object, that is the Memo,
    // which recurses indefinitely.
    void
    Memo::print(std::ostream& o) const
    {
      elle::fprintf(o, "%s(%s)", elle::type_info(*this), this->backend());
    }

    memo::Memo&
    Memo::backend() const
    {
      return this->_memo;
    }
  }
}
