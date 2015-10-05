#ifndef INFINIT_MODEL_DOUGHNUT_CONFLICT_HH
# define INFINIT_MODEL_DOUGHNUT_CONFLICT_HH

# include <elle/Error.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Conflict
        : public elle::Error
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef Conflict Self;
        typedef Error Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Conflict(std::string const& reason);

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        Conflict(elle::serialization::SerializerIn& input);
      };
    }
  }
}

#endif