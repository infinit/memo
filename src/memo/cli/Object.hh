#pragma once

#include <elle/attribute.hh>

#include <elle/das/bound-method.hh>
#include <elle/das/cli.hh>

#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>

namespace memo
{
  namespace cli
  {
    template <typename Self, typename Owner = Memo, typename Cli = Memo>
    class Object;

    template <typename Self, typename Owner, typename Cli>
    class Object
      : public elle::das::named::Function<void (decltype(help = false))>
    {
    public:
      Object(Cli& memo);
      void
      help(std::ostream& s);
      void
      call(bool help);
      void
      apply(Cli& cli, std::vector<std::string>& args);
      template <typename Symbol, typename ... Args>
      static
      auto
      binding(Symbol const&, Args&& ... args)
        -> decltype(elle::das::named::function(
                      elle::das::bind_method<Symbol, Self>(std::declval<Self&>()),
                      std::forward<Args>(args)...));
      template <typename Symbol, typename ... Args>
      auto
      bind(Symbol const& s, Args&& ... args)
        -> decltype(binding(s, std::forward<Args>(args)...));
      ELLE_ATTRIBUTE_R(Cli&, cli);
      ELLE_ATTRIBUTE_R(elle::das::cli::Options, options, protected);
    };

    template <typename Symbol, typename Object, typename Cli = Memo>
    struct mode_call
    {
      using type = bool;
      static
      bool
      value(Cli& memo,
            Object& o,
            std::vector<std::string>& args,
            bool& found)
      {
        if (!found && elle::das::cli::option_name_from_c(Symbol::name()) == args[0])
        {
          found = true;
          args.erase(args.begin());
          Symbol::attr_get(o).apply(memo, args);
          return true;
        }
        else
          return false;
      }
    };
  }
}
