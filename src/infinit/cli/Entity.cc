#include <elle/Exit.hh>
#include <elle/printf.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/cli/Silo.hh>
#include <infinit/cli/User.hh>

namespace infinit
{
  namespace cli
  {
    template <typename Self>
    Entity<Self>::Entity(Infinit& infinit)
      : EntityCallable<Self>(
        das::bind_method(*this, cli::call), cli::help = false)
      , _cli(infinit)
    {
      this->_options.emplace(
        "help", das::cli::Option('h', "show this help message"));
    }

    template <typename T, typename Names>
    struct _find_name
    {};

    template <typename T, typename Head, typename ... Tail>
    struct _find_name<T, elle::meta::List<Head, Tail...>>
      : public std::conditional<
          std::is_same<typename Head::template attr_type<Infinit>::type,
                       T>::value,
          Head,
          _find_name<T, elle::meta::List<Tail...>>>::type
    {};

    template <typename T>
    struct find_name
      : public _find_name<T, Infinit::Entities>
    {};

    template <typename Self>
    void
    Entity<Self>::help(std::ostream& s)
    {
      using Symbol = find_name<Self>;
      Infinit::usage(
        s, elle::sprintf("%s [MODE|--help]", Symbol::name()));
      elle::fprintf(s,
                    "Infinit %s management utility.\n"
                    "\n"
                    "Modes:\n",
                    Symbol::name());
      Self::Modes::template map<help_list>::value(s);
      elle::fprintf(s,
                    "\n"
                    "Options:\n");
      das::cli::help(static_cast<Self&>(*this), s, this->options());
    }

    template <typename Self>
    void
    Entity<Self>::call(bool help)
    {
      if (help)
        this->help(std::cout);
      else
      {
        this->help(std::cerr);
        throw elle::Exit(1);
      }
    }

    template
    class Entity<User>;
    template
    class Entity<Silo>;
  }
}
