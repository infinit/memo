#ifndef INFINIT_DESCRIPTOR_TEMPLATED_BASE_DESCRIPTOR_HH
# define INFINIT_DESCRIPTOR_TEMPLATED_BASE_DESCRIPTOR_HH

# include <boost/optional.hpp>

# include <elle/Error.hh>

# include <infinit/descriptor/BaseDescriptor.hh>

namespace infinit
{
  namespace descriptor
  {
    template <typename T>
    struct TemplatedBaseDescriptor
      : public BaseDescriptor
    {
      /*-------------.
      | Construction |
      `-------------*/

      TemplatedBaseDescriptor(std::string name,
                              boost::optional<std::string> description = {});
      TemplatedBaseDescriptor(TemplatedBaseDescriptor const& descriptor);

      static
      bool
      permit_name_slash()
      {
        return true;
      }

      static
      void
      check_name(std::string const& name);

      static
      void
      check_description(std::string const& description);

      /*--------------.
      | Serialization |
      `--------------*/

      TemplatedBaseDescriptor(elle::serialization::SerializerIn& s);

    };
  }
}

# include <infinit/descriptor/TemplatedBaseDescriptor.hxx>

#endif // INFINIT_DESCRIPTOR_TEMPLATED_BASE_DESCRIPTOR_HH
