#include <memo/overlay/Kalimero.hh>

#include <elle/log.hh>

#include <memo/model/doughnut/Local.hh>

namespace memo
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

    auto
    Kalimero::_allocate(model::Address address, int n) const
      -> MemberGenerator
    {
      return this->_lookup(address, n, false);
    }

    auto
    Kalimero::_lookup(model::Address address, int n, bool) const
      -> MemberGenerator
    {
      if (n != 1)
        elle::err("kalimero cannot fetch several (%s) nodes", n);
      else if (!this->local())
        elle::err("kalimero can only be a server");
      else
        return [this] (std::function<void (Kalimero::WeakMember)> yield)
        {
          yield(this->local());
        };
    }

    auto
    Kalimero::_lookup_node(model::Address address) const
      -> WeakMember
    {
      return this->local();
    }

    /*-----------.
    | Monitoring |
    `-----------*/

    std::string
    Kalimero::type_name() const
    {
      return "kalimero";
    }

    elle::json::Array
    Kalimero::peer_list() const
    {
      return {};
    }

    elle::json::Object
    Kalimero::stats() const
    {
      return {{"type", this->type_name()}};
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

    std::unique_ptr<memo::overlay::Overlay>
    KalimeroConfiguration::make(std::shared_ptr<model::doughnut::Local> local,
                                model::doughnut::Doughnut* dht)
    {
      return std::make_unique<Kalimero>(dht, std::move(local));
    }

    static const
    elle::serialization::Hierarchy<memo::overlay::Configuration>::
    Register<KalimeroConfiguration> _registerKalimeroOverlayConfig("kalimero");
  }
}
