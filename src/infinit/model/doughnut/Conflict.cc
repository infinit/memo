#include <elle/serialization/Serializer.hh>

#include <infinit/model/doughnut/Conflict.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Conflict::Conflict(std::string const& reason)
        : Super(elle::sprintf("conflict editing block: %s", reason))
      {}

      Conflict::Conflict(
        elle::serialization::SerializerIn& input)
        : Super(input)
      {}

      static const elle::serialization::Hierarchy<elle::Exception>::
      Register<Conflict> _register_serialization;
    }
  }
}