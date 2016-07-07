#ifndef INFINIT_MODEL_DOUGHNUT_PROTOCOL
# define INFINIT_MODEL_DOUGHNUT_PROTOCOL

# include <elle/serialization/Serializer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      enum class Protocol
      {
        tcp = 1,
        utp = 2,
        all = 3
      };
    }
  }
}

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<infinit::model::doughnut::Protocol>
    {
      typedef std::string Type;
      static
      std::string
      convert(infinit::model::doughnut::Protocol p);
      static
      infinit::model::doughnut::Protocol
      convert(std::string const& repr);
    };
  }
}

#endif
