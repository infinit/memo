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

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Kalimero::WeakMember>
    Kalimero::_lookup(model::Address address, int n, Operation op) const
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
    Kalimero::_lookup_node(model::Address address)
    {
      return this->local();
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
    KalimeroConfiguration::make(std::vector<Endpoints> const& hosts,
                                std::shared_ptr<model::doughnut::Local> local,
                                model::doughnut::Doughnut* dht)
    {
      if (!hosts.empty())
        elle::err("kalimero cannot access other nodes: %s", hosts);
      return elle::make_unique<Kalimero>(dht, std::move(local));
    }

    static const
    elle::serialization::Hierarchy<infinit::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
