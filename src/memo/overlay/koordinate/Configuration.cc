#include <memo/overlay/koordinate/Configuration.hh>
#include <memo/overlay/koordinate/Koordinate.hh>

namespace memo
{
  namespace overlay
  {
    namespace koordinate
    {
      /*-------------.
      | Construction |
      `-------------*/

      Configuration::Configuration(Backends backends)
        : Super()
        , _backends(std::move(backends))
      {
        this->_validate();
      }

      void
      Configuration::_validate() const
      {
        if (this->_backends.empty())
          elle::err("Koordinate backends list should not be empty");
      }

      /*---------.
      | Clonable |
      `---------*/

      std::unique_ptr<overlay::Configuration>
      Configuration::clone() const
      {
        Backends backends;
        for (auto& backend: this->_backends)
          backends.emplace_back(backend->clone());
        return std::make_unique<Self>(std::move(backends));
      }

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
        s.serialize("backends", this->_backends);
      }

      static const
      elle::serialization::Hierarchy<overlay::Configuration>::
      Register<Configuration> _registerConfiguration("koordinate");

      /*--------.
      | Factory |
      `--------*/

      std::unique_ptr<memo::overlay::Overlay>
      Configuration::make(std::shared_ptr<model::doughnut::Local> local,
                          model::doughnut::Doughnut* doughnut)
      {
        Koordinate::Backends backends;
        for (auto& conf: this->_backends)
          backends.emplace_back(conf->make(local, doughnut));
        return std::make_unique<Koordinate>(
          doughnut, std::move(local), std::move(backends));
      }
    }
  }
}
