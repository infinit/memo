#pragma once

#include <elle/Error.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class ValidationFailed
        : public elle::Error
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = memo::model::doughnut::ValidationFailed;
        using Super = elle::Error;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ValidationFailed(std::string const& reason);

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ValidationFailed(elle::serialization::SerializerIn& input);
      };
    }
  }
}
