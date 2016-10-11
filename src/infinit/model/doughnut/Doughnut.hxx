#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HXX
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HXX

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template <typename T>
      void
      Doughnut::service_add(std::string const& type,
                            std::string const& name,
                            T const& value)
      {
        this->service_add(
          type, name, elle::serialization::binary::serialize(value));
      }
    }
  }
}

#endif
