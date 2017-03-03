#ifndef INFINIT_MODEL_DOUGHNUT_HANDSHAKE_FAILED_HH
# define INFINIT_MODEL_DOUGHNUT_HANDSHAKE_FAILED_HH

# include <elle/Error.hh>

namespace infinit
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
        using Self = infinit::model::doughnut::HandshakeFailed;
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

#endif