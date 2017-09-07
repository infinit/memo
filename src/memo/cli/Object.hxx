#include <memo/cli/Object.hh>

#include <elle/Exit.hh>
#include <elle/print.hh>
#include <elle/printf.hh>

#include <memo/cli/Memo.hh>
#include <memo/cli/utility.hh>
#include <memo/model/prometheus.hh>

namespace memo
{
  namespace cli
  {
    template <typename Self, typename Owner, typename Cli>
    Object<Self, Owner, Cli>::Object(Cli& cli)
      : elle::das::named::Function<void (decltype(cli::help = false))>(
        elle::das::bind_method(*this, cli::call), cli::help = false)
      , _cli(cli)
    {
      this->_options.emplace(
        "help", elle::das::cli::Option('h', "show this help message"));
    }

    namespace
    {
      template <typename T, typename Owner, typename Names>
      struct _find_name
      {};

      template <typename T, typename Owner, typename Head, typename ... Tail>
      struct _find_name<T, Owner, elle::meta::List<Head, Tail...>>
        : public std::conditional_t<
            std::is_same<typename Head::template attr_type<Owner>,
                         T>::value,
            Head,
            _find_name<T, Owner,elle::meta::List<Tail...>>>
      {};

      template <typename T, typename Owner>
      using find_name = _find_name<T, Owner, typename Owner::Objects>;

      template <typename Symbol, typename Object>
      struct help_modes
      {
        using type = bool;
        static
        bool
        value(std::ostream& s, Object const& object, std::string const& object_name)
        {
          elle::print(s, "  %-10s %s\n",
                      elle::das::cli::option_name_from_c(Symbol::name()),
                      elle::print(Symbol::attr_get(object).description,
                           {
                             {"action",  elle::print("to %s", Symbol::name())},
                             {"hub",     beyond(true)},
                             {"object",  object_name},
                             {"objects", plural(object_name)},
                             {"verb",    Symbol::name()},
                           }));
          return true;
        }
      };
    }

    template <typename Self, typename Owner, typename Cli>
    void
    Object<Self, Owner, Cli>::help(std::ostream& s)
    {
      using Symbol = find_name<Self, Owner>;
      Cli::usage(
        s, elle::print("%s [MODE|--help]", Symbol::name()));
      elle::fprintf(s,
                    "Memo %s management utility.\n"
                    "\n"
                    "Modes:\n",
                    Symbol::name());
      Self::Modes::template map<help_modes, Self>
        ::value(s, static_cast<Self&>(*this), Symbol::name());
      elle::fprintf(s,
                    "\n"
                    "Options:\n"
                    "%s",
                    elle::das::cli::help(static_cast<Self&>(*this), this->options()));
    }

    template <typename Self, typename Owner, typename Cli>
    void
    Object<Self, Owner, Cli>::call(bool help)
    {
      if (help)
        this->help(std::cout);
      else
      {
        this->help(std::cerr);
        throw elle::Exit(1);
      }
    }

    template <typename Self, typename Owner, typename Cli>
    void
    Object<Self, Owner, Cli>::apply(Cli&, std::vector<std::string>& args)
    {
      using Symbol = find_name<Self, Owner>;
      try
      {
        if (args.empty() || elle::das::cli::is_option(args[0], this->options()))
          elle::das::cli::call(*this, args, this->options());
        else
        {
          bool found = false;
          Self::Modes::template map<mode_call, Self, Cli>::value(
            this->cli(), static_cast<Self&>(*this), args, found);
          if (!found)
            elle::err<elle::das::cli::Error>(
              "unknown mode for object %s: %s", Symbol::name(), args[0]);
        }
      }
      catch (CLIError& e)
      {
        throw;
      }
      catch (elle::das::cli::Error const& e)
      {
        auto ex = CLIError(e.what());
        ex.object(Symbol::name());
        throw ex;
      }
    }

    template <typename Self, typename Owner, typename Cli>
    template <typename Symbol, typename ... Args>
    auto
    Object<Self, Owner, Cli>::bind(Symbol const& s, Args&& ... args)
      -> decltype(binding(s, std::forward<Args>(args)...))
    {
      return elle::das::named::function(
        elle::das::bind_method<Symbol, Self>(static_cast<Self&>(*this)),
        std::forward<Args>(args)...);
    }

    template <typename Self, typename Sig, typename Symbol>
    void
    Mode<Self, Sig, Symbol>::apply(Memo& cli,
                                   std::vector<std::string>& args)
    {
      // Add the options that are common to all the modes.
      auto const options = this->options;
      auto const f = this->prototype().extend(
        help = false
        , as = cli.default_user_name()
        , cli::compatibility_version = boost::none
        , script = false
        , log = boost::optional<std::string>()
        , prometheus = boost::optional<std::string>()
      );
      auto const verb = cli.command_line().at(1);
      auto const subst = [&](auto const& f)
        {
          return elle::print(f,
            {
              {"action",  elle::print("to %s", verb)},        // to delete
              {"hub",     beyond(true)},                      // the hub
              {"object",  cli.command_line().at(0)},          // log
              {"objects", plural(cli.command_line().at(0))},  // logs
              {"verb",    verb},                              // delete
            });
        };
      auto const show_help = [&] (std::ostream& s)
        {
          cli.usage(s, subst("{object} {verb} [OPTIONS]"));
          s << subst(this->description) << "\n\nOptions:\n";
          {
            std::stringstream buffer;
            buffer << elle::das::cli::help(f, options);
            s << subst(buffer.str());
          }
        };
      try
      {
        elle::das::cli::call(
          f,
          [&] (bool help,
               std::string as,
               boost::optional<elle::Version> const& compatibility_version,
               bool script,
               boost::optional<std::string> log,
               boost::optional<std::string> prometheus,
               auto&& ... args)
          {
            cli.as(as);
            cli.script(script);
            if (compatibility_version)
            {
              ensure_version_is_supported(*compatibility_version);
              cli.compatibility_version(std::move(compatibility_version));
            }
            if (log)
              elle::log::logger_add(elle::log::make_logger(*log));
            if (prometheus)
              memo::prometheus::endpoint(*prometheus);
            if (help)
              show_help(std::cout);
            else
              this->function()(std::forward<decltype(args)>(args)...);
          },
          args,
          options);
      }
      catch (elle::das::cli::Error const& e)
      {
        auto ex = CLIError(e.what());
        ex.object(subst("{object} {verb}"));
        throw ex;
      }
    }
  }
}
