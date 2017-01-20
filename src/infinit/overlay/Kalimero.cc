#include <infinit/overlay/Kalimero.hh>

#include <elle/log.hh>

#include <infinit/model/doughnut/Local.hh>

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Kalimero::Kalimero(model::doughnut::Doughnut* dht,
                       std::shared_ptr<model::doughnut::Local> local)
      : Overlay(dht, std::move(local))
    {}

    /*------.
    | Peers |
    `------*/

    void
    Kalimero::_discover(NodeLocations const&)
    {
      elle::err("Kalimero cannot discover new nodes");
    }

    bool
    Kalimero::_discovered(model::Address id)
    {
      return id == this->id();
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Overlay::WeakMember>
    Kalimero::_allocate(model::Address address, int n) const
    {
      return this->_lookup(address, n, false);
    }

    reactor::Generator<Kalimero::WeakMember>
    Kalimero::_lookup(model::Address address, int n, bool) const
    {
      if (n != 1)
      {
        throw elle::Error(
          elle::sprintf("kalimero cannot fetch several (%s) nodes", n));
      }
      if (!this->local())
        throw elle::Error("kalimero can only be a server");
      return reactor::generator<Kalimero::WeakMember>(
        [this] (std::function<void (Kalimero::WeakMember)> yield)
        {
          yield(this->local());
        });
    }

    Overlay::WeakMember
    Kalimero::_lookup_node(model::Address address) const
    {
      return this->local();
    }

    /*-----------.
    | Monitoring |
    `-----------*/

    std::string
    Kalimero::type_name()
    {
      return "kalimero";
    }

    elle::json::Array
    Kalimero::peer_list()
    {
      return elle::json::Array();
    }

    elle::json::Object
    Kalimero::stats()
    {
      elle::json::Object res;
      res["type"] = this->type_name();
      return res;
    }

    KalimeroConfiguration::KalimeroConfiguration()
    {}

    KalimeroConfiguration::KalimeroConfiguration(
      elle::serialization::SerializerIn& input)
      : Configuration(input)
    {}

    void
    KalimeroConfiguration::serialize(elle::serialization::Serializer& s)
    {
      Configuration::serialize(s);
    }

    std::unique_ptr<infinit::overlay::Overlay>
    KalimeroConfiguration::make(std::shared_ptr<model::doughnut::Local> local,
                                model::doughnut::Doughnut* dht)
    {
      return elle::make_unique<Kalimero>(dht, std::move(local));
    }

    static const
    elle::serialization::Hierarchy<infinit::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
