#pragma once

#include <boost/optional.hpp>

#include <elle/Error.hh>

#include <memo/descriptor/BaseDescriptor.hh>

namespace memo
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
      std::string
      name_regex();

      static
      std::string
      description_regex();

      static
      void
      check_name(std::string const& name);

      static
      void
      check_description(boost::optional<std::string> const& description);

      /*--------------.
      | Serialization |
      `--------------*/

      TemplatedBaseDescriptor(elle::serialization::SerializerIn& s);

    };
  }
}

#include <memo/descriptor/TemplatedBaseDescriptor.hxx>
