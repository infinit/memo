#pragma once

#include <elle/Error.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class HandshakeFailed
        : public elle::Error
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = memo::model::doughnut::HandshakeFailed;
        using Super = elle::Error;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        HandshakeFailed(std::string const& reason);

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        HandshakeFailed(elle::serialization::SerializerIn& input);
      };
    }
  }
}
