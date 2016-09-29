#include <infinit/overlay/kouncil/Configuration.hh>
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

      Configuration::Configuration()
        : Super()
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
      {}

      static const
      elle::serialization::Hierarchy<overlay::Configuration>::
      Register<Configuration> _registerConfiguration("kouncil");

      /*--------.
      | Factory |
      `--------*/

      std::unique_ptr<infinit::overlay::Overlay>
      Configuration::make(std::shared_ptr<model::doughnut::Local> local,
                          model::doughnut::Doughnut* doughnut)
      {
        return elle::make_unique<Kouncil>(doughnut, std::move(local));
      }
    }
  }
}
