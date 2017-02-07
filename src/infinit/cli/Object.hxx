#include <infinit/cli/Object.hh>

#include <elle/Exit.hh>
#include <elle/printf.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>

namespace infinit
{
  namespace cli
  {
    template <typename Symbol, typename ObjectSymbol, typename Object>
    struct mode_call
    {
      using type = bool;
      static
      bool
      value(infinit::cli::Infinit& infinit,
            Object& o,
            std::vector<std::string>& args,
            bool& found)
      {
        if (!found && das::cli::option_name_from_c(Symbol::name()) == args[0])
        {
          found = true;
          args.erase(args.begin());
          auto& mode = Symbol::attr_get(o);
          _handle(infinit, o, mode, args);
          return true;
        }
        else
          return false;
      }

      template <typename M>
      static
      void
      _handle(Infinit& infinit,
              Object& o,
              Mode<M>& mode,
              std::vector<std::string>& args)
      {
        auto options = mode.options;
        auto f = mode.prototype().extend(
          help = false,
          cli::compatibility_version = boost::none,
          script = false,
          as = infinit.default_user_name());
        auto show_help = [&] (std::ostream& s)
          {
            auto action = Symbol::name();
            {
              auto dash = action.find("_");
              if (dash != std::string::npos)
                action = action.substr(0, dash);
            }
            auto vars = VarMap{
              {"action", elle::sprintf("to %s", action)},
              {"hub", beyond(true)},
              {"object", ObjectSymbol::name()},
              {"verb", action},
            };
            Infinit::usage(
              s, elle::sprintf(
                "%s %s [OPTIONS]",
                das::cli::option_name_from_c(ObjectSymbol::name()),
                das::cli::option_name_from_c(Symbol::name())));
            s << vars.expand(mode.description) << "\n\nOptions:\n";
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
      }

      template <typename SubObject, typename Owner>
      static
      void
      _handle(Infinit& infinit,
              Object& o,
              cli::Object<SubObject, Owner>& sub,
              std::vector<std::string>& args)
      {
        static_cast<SubObject&>(sub).recurse(args);
      }
    };

    template <typename Self, typename Owner>
    Object<Self, Owner>::Object(Infinit& infinit)
      : ObjectCallable<Self, Owner>(
        das::bind_method(*this, cli::call), cli::help = false)
      , _cli(infinit)
    {
      this->_options.emplace(
        "help", das::cli::Option('h', "show this help message"));
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
      struct find_name
        : public _find_name<T, Owner, typename Owner::Objects>
      {};

      template <typename Symbol, typename Object>
      struct help_modes
      {
        using type = bool;
        static
        bool
        value(std::ostream& s, Object const& object)
        {
          auto vars = VarMap{
            {"action", elle::sprintf("to %s", Symbol::name())},
            {"hub", beyond(true)},
          };
          elle::fprintf(s, "  %-10s %s\n",
                        das::cli::option_name_from_c(Symbol::name()),
                        vars.expand(Symbol::attr_get(object).description));
          return true;
        }
      };
    }

    template <typename Self, typename Owner>
    void
    Object<Self, Owner>::help(std::ostream& s)
    {
      using Symbol = find_name<Self, Owner>;
      Infinit::usage(
        s, elle::sprintf("%s [MODE|--help]", Symbol::name()));
      elle::fprintf(s,
                    "Infinit %s management utility.\n"
                    "\n"
                    "Modes:\n",
                    Symbol::name());
      Self::Modes::template map<help_modes, Self>
        ::value(s, static_cast<Self&>(*this));
      elle::fprintf(s,
                    "\n"
                    "Options:\n");
      das::cli::help(static_cast<Self&>(*this), s, this->options());
    }

    template <typename Self, typename Owner>
    void
    Object<Self, Owner>::call(bool help)
    {
      if (help)
        this->help(std::cout);
      else
      {
        this->help(std::cerr);
        throw elle::Exit(1);
      }
    }

    template <typename Self, typename Owner>
    void
    Object<Self, Owner>::recurse(std::vector<std::string>& args)
    {
      using Symbol = find_name<Self, Owner>;
      try
      {
        if (args.empty() || das::cli::is_option(args[0], this->options()))
          das::cli::call(*this, args, this->options());
        else
        {
          bool found = false;
          Self::Modes::template map<mode_call, Symbol, Self>::value(
            this->cli(), static_cast<Self&>(*this), args, found);
          if (!found)
            throw das::cli::Error(
              elle::sprintf("unknown mode for object %s: %s",
                            Symbol::name(), args[0]));
        }
      }
      catch (CLIError const& e)
      {
        throw;
      }
      catch (das::cli::Error const& e)
      {
        std::stringstream s;
        s << e.what() << "\n\n";
        this->help(s);
        // Discard trailing newline
        auto msg = s.str();
        msg = msg.substr(0, msg.size() - 1);
        throw CLIError(msg);
      }
    }

    template <typename Self, typename Owner>
    template <typename Symbol, typename ... Args>
    auto
    Object<Self, Owner>::bind(Symbol const& s, Args&& ... args)
      -> decltype(binding(s, std::forward<Args>(args)...))
    {
      return das::named::function(
        das::bind_method<Self, Symbol>(static_cast<Self&>(*this)),
        std::forward<Args>(args)...);
    }
  }
}
