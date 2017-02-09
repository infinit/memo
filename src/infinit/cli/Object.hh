#pragma once

#include <elle/attribute.hh>

#include <das/bound-method.hh>
#include <das/cli.hh>

#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>

namespace infinit
{
  namespace cli
  {
    template <typename Self, typename Owner = Infinit>
    class Object;

    template <typename Self, typename Owner = Infinit>
    using ObjectCallable =
      decltype(
        das::named::function(
          das::bind_method(std::declval<Object<Self, Owner>&>(), cli::call),
          help = false));

    template <typename Self, typename Owner>
    class Object
      : public ObjectCallable<Self, Owner>
    {
    public:
      Object(Infinit& infinit);
      void
      help(std::ostream& s);
      void
      call(bool help);
      void
      apply(Infinit& cli, std::vector<std::string>& args);
      template <typename Symbol, typename ... Args>
      static
      auto
      binding(Symbol const&, Args&& ... args)
        -> decltype(das::named::function(
                      das::bind_method<Self, Symbol>(std::declval<Self&>()),
                      std::forward<Args>(args)...));
      template <typename Symbol, typename ... Args>
      auto
      bind(Symbol const& s, Args&& ... args)
        -> decltype(binding(s, std::forward<Args>(args)...));
      ELLE_ATTRIBUTE_R(Infinit&, cli);
      ELLE_ATTRIBUTE_R(das::cli::Options, options, protected);
      // ELLE_ATTIRBUTE_R(elle::unordered_map<std::string, >);
    };

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
          Symbol::attr_get(o).apply(infinit, args);
          return true;
        }
        else
          return false;
      }
    };
  }
}
