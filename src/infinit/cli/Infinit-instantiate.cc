#include <infinit/cli/Infinit.hh>
#include <infinit/cli/Infinit-template.hxx>

#include <elle/string/algorithm.hh>

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
    static
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

    static
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
    struct mode_call
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
        if (!found && das::cli::option_name_from_c(Symbol::name()) == args[0])
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
              Infinit::usage(
                s, elle::sprintf(
                  "%s %s [OPTIONS]",
                  das::cli::option_name_from_c(ObjectSymbol::name()),
                  das::cli::option_name_from_c(Symbol::name())));
              s << mode.help << "\n\nOptions:\n";
              {
                std::stringstream buffer;
                das::cli::help(f, buffer, options);
                auto res = buffer.str();
                std::ostream_iterator<char, char> out(s);
                auto action = Symbol::name();
                {
                  auto dash = action.find("_");
                  if (dash != std::string::npos)
                    action = action.substr(0, dash);
                }
                elle::unordered_map<std::string, std::string> fmt{
                  {"action", elle::sprintf("to %s", action)},
                  {"hub", beyond(true)},
                  {"type", ObjectSymbol::name()},
                  {"verb", action},
                };
                s << boost::regex_replace(
                  buffer.str(),
                  boost::regex("\\{\\w+\\}"),
                  [&] (boost::smatch in)
                  {
                    auto k = in.str();
                    return fmt.at(k.substr(1, k.size() - 2));
                  });
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
        typename Symbol::template attr_type<infinit::cli::Infinit>::type;
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
  }
}

namespace infinit
{
  namespace cli
  {
    template
    struct object<std::decay<decltype(cli::INFINIT_CLI_OBJECT)>::type>;
  }
}
