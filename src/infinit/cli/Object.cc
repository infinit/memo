#include <infinit/cli/Object.hh>

#include <elle/Exit.hh>
#include <elle/printf.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>

namespace infinit
{
  namespace cli
  {
    template <typename Self>
    Object<Self>::Object(Infinit& infinit)
      : ObjectCallable<Self>(
        das::bind_method(*this, cli::call), cli::help = false)
      , _cli(infinit)
    {
      this->_options.emplace(
        "help", das::cli::Option('h', "show this help message"));
    }

    namespace
    {
      template <typename T, typename Names>
      struct _find_name
      {};

      template <typename T, typename Head, typename ... Tail>
      struct _find_name<T, elle::meta::List<Head, Tail...>>
        : public std::conditional_t<
            std::is_same<typename Head::template attr_type<Infinit>,
                         T>::value,
            Head,
            _find_name<T, elle::meta::List<Tail...>>>
      {};

      template <typename T>
      struct find_name
        : public _find_name<T, Infinit::Objects>
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
                        vars.expand(Symbol::attr_get(object).help));
          return true;
        }
      };
    }

    template <typename Self>
    void
    Object<Self>::help(std::ostream& s)
    {
      using Symbol = find_name<Self>;
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

    template <typename Self>
    void
    Object<Self>::call(bool help)
    {
      if (help)
        this->help(std::cout);
      else
      {
        this->help(std::cerr);
        throw elle::Exit(1);
      }
    }

    /*----------------------.
    | Instantiate objects.  |
    `----------------------*/

    template class Object<ACL>;
    template class Object<Block>;
    template class Object<Credentials>;
#if INFINIT_WITH_DAEMON
    template class Object<Daemon>;
#endif
    template class Object<Device>;
    template class Object<Doctor>;
    template class Object<Drive>;
    template class Object<Journal>;
    template class Object<LDAP>;
    template class Object<Network>;
    template class Object<Passport>;
    template class Object<Silo>;
    template class Object<User>;
    template class Object<Volume>;
  }
}
