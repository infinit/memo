#include <infinit/overlay/Kalimero.hh>

#include <elle/log.hh>

#include <infinit/model/doughnut/Local.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Kalimero");

namespace infinit
{
  namespace overlay
  {
    Kalimero::Kalimero(model::doughnut::Doughnut* dht,
                       model::Address node_id,
                       std::shared_ptr<model::doughnut::Local> local)
      : Overlay(dht, std::move(local), std::move(node_id))
    {}

    reactor::Generator<Kalimero::Member>
    Kalimero::_lookup(model::Address address, int n, Operation op) const
    {
      if (n != 1)
      {
        throw elle::Error(
          elle::sprintf("kalimero cannot fetch several (%s) nodes", n));
      }
      if (!this->local())
        throw elle::Error("kalimero can only be a server");
      return reactor::generator<Kalimero::Member>(
        [this] (std::function<void (Kalimero::Member)> yield)
        {
          yield(this->local());
        });
    }

    Overlay::Member
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
    KalimeroConfiguration::make(model::Address id,
                                NodeEndpoints const& hosts,
                                std::shared_ptr<model::doughnut::Local> local,
                                model::doughnut::Doughnut* dht)
    {
      if (!hosts.empty())
        throw elle::Error(
          elle::sprintf("kalimero cannot access other nodes (%s)", hosts));
      return elle::make_unique<Kalimero>(dht, id, std::move(local));
    }

    static const
    elle::serialization::Hierarchy<infinit::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
