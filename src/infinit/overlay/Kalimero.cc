#include <infinit/overlay/Kalimero.hh>

#include <elle/log.hh>

#include <infinit/model/doughnut/Local.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Kalimero");

namespace infinit
{
  namespace overlay
  {
    Kalimero::Kalimero()
      : _local()
    {}

    Kalimero::Members
    Kalimero::_lookup(model::Address address, int n, Operation op) const
    {
      if (n != 1)
        throw elle::Error(
          elle::sprintf("kalimero cannot fetch several (%s) nodes", n));
      auto local = this->_local.lock();
      if (!local)
        throw elle::Error("kalimero can only be a server");
      Members res;
      res.emplace_back(local);
      return res;
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
    {}

    void
    KalimeroConfiguration::serialize(elle::serialization::Serializer& s)
    {}

    std::unique_ptr<infinit::overlay::Overlay>
    KalimeroConfiguration::make(std::vector<std::string> const& hosts, bool,
      model::doughnut::Doughnut*)
    {
      if (!hosts.empty())
        throw elle::Error(
          elle::sprintf("kalimero cannot access other nodes (%s)", hosts));
      return elle::make_unique<Kalimero>();
    }

    static const
    elle::serialization::Hierarchy<infinit::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
