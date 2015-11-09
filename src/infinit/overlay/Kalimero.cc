#include <infinit/overlay/Kalimero.hh>

#include <elle/log.hh>

#include <infinit/model/doughnut/Local.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Kalimero");

namespace infinit
{
  namespace overlay
  {
    Kalimero::Kalimero(model::Address node_id)
      : Overlay(std::move(node_id))
      , _local()
    {}

    reactor::Generator<Kalimero::Member>
    Kalimero::_lookup(model::Address address, int n, Operation op) const
    {
      if (n != 1)
      {
        throw elle::Error(
          elle::sprintf("kalimero cannot fetch several (%s) nodes", n));
      }
      auto local = this->_local.lock();
      if (!local)
        throw elle::Error("kalimero can only be a server");
      return reactor::generator<Kalimero::Member>(
        [local] (std::function<void (Kalimero::Member)> yield)
        {
          yield(local);
        });
    }

    Overlay::Member
    Kalimero::_lookup_node(model::Address address)
    {
      return this->_local.lock();
    }

    void
    Kalimero::register_local(std::shared_ptr<model::doughnut::Local> local)
    {
      ELLE_TRACE("%s: register local: %s", *this, *local);
      this->_local = local;
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
                                NodeEndpoints const& hosts, bool,
      model::doughnut::Doughnut*)
    {
      if (!hosts.empty())
        throw elle::Error(
          elle::sprintf("kalimero cannot access other nodes (%s)", hosts));
      return elle::make_unique<Kalimero>(id);
    }

    static const
    elle::serialization::Hierarchy<infinit::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
