#include <elle/serialization/Serializer.hh>

#include <infinit/model/doughnut/ValidationFailed.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      ValidationFailed::ValidationFailed(std::string const& reason)
        : Super(elle::sprintf("block validation failed: %s", reason))
      {}

      ValidationFailed::ValidationFailed(
        elle::serialization::SerializerIn& input)
        : Super(input)
      {}

      static const elle::serialization::Hierarchy<elle::Exception>::
      Register<ValidationFailed> _register_serialization;
    }
  }
}
