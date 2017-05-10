#include <infinit/overlay/kouncil/Configuration.hh>

#include <elle/serialization/json/Error.hh>

#include <infinit/overlay/kouncil/Kouncil.hh>

namespace infinit
{
  namespace overlay
  {
    namespace kouncil
    {
      /*-------------.
      | Construction |
      `-------------*/

      Configuration::Configuration(std::chrono::seconds eviction_delay)
        : Super()
        , _eviction_delay{eviction_delay}
      {
        this->_validate();
      }

      void
      Configuration::_validate() const
      {}

      /*--------------.
      | Serialization |
      `--------------*/

      Configuration::Configuration(elle::serialization::SerializerIn& input)
        : Super()
      {
        this->serialize(input);
      }

      void
      Configuration::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        s.serialize("eviction_delay", _eviction_delay);
      }

      namespace
      {
        static auto const _reg =
          elle::serialization::Hierarchy<overlay::Configuration>::
          Register<Configuration>("kouncil");
      }

      /*--------.
      | Factory |
      `--------*/

      std::unique_ptr<infinit::overlay::Overlay>
      Configuration::make(std::shared_ptr<model::doughnut::Local> local,
                          model::doughnut::Doughnut* doughnut)
      {
        return std::make_unique<Kouncil>(doughnut,
                                         std::move(local),
                                         this->_eviction_delay);
      }
    }
  }
}
