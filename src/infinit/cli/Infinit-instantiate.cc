#include <infinit/cli/Infinit.hh>
#include <infinit/cli/Infinit-template.hxx>
#include <infinit/cli/utility.hh> // ensure_version_is_supported

namespace infinit
{
  namespace cli
  {
    template <typename Symbol, typename ObjectSymbol>
    struct mode_call
    {
      using Object = typename ObjectSymbol::template attr_type<Infinit>;
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
              s << vars.expand(mode.help) << "\n\nOptions:\n";
              {
                std::stringstream buffer;
                buffer << das::cli::help(f, options);
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
    bool
    object<Symbol>::value(infinit::cli::Infinit& infinit,
                          std::vector<std::string>& args,
                          bool& found)
    {
      using Object =
        typename Symbol::template attr_type<infinit::cli::Infinit>;
      auto& object = Symbol::attr_get(infinit);
      if (!found && das::cli::option_name_from_c(Symbol::name()) == args[0])
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
            Object::Modes::template map<mode_call, Symbol>::value(
              infinit, object, args, found);
            if (!found)
              elle::err<CLIError>("unknown mode for object %s: %s",
                                  Symbol::name(), args[0]);
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

    template
    struct object<std::decay<decltype(cli::INFINIT_CLI_OBJECT)>::type>;
  }
}
