#ifndef INFINIT_MODEL_DOUGHNUT_VALIDATION_FAILED_HH
# define INFINIT_MODEL_DOUGHNUT_VALIDATION_FAILED_HH

# include <elle/Error.hh>

namespace infinit
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
        using Self = infinit::model::doughnut::ValidationFailed;
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

#endif
