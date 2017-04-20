#pragma once

#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    class Kalimero
      : public Overlay
    {
    /*-------------.
    | Construction |
    `-------------*/
    public:
      Kalimero(model::doughnut::Doughnut* dht,
               std::shared_ptr<model::doughnut::Local> local);

    /*------.
    | Peers |
    `------*/
    protected:
      void
      _discover(NodeLocations const& peers) override;
      bool
      _discovered(model::Address id) override;

    /*-------.
    | Lookup |
    `-------*/
    protected:
      MemberGenerator
      _allocate(model::Address address, int n) const override;
      MemberGenerator
      _lookup(model::Address address, int n, bool fast) const override;
      Overlay::WeakMember
      _lookup_node(model::Address address) const override;

    /*-----------.
    | Monitoring |
    `-----------*/
    public:
      std::string
      type_name() const override;
      elle::json::Array
      peer_list() const override;
      elle::json::Object
      stats() const override;
    };

    struct KalimeroConfiguration
      : public Configuration
    {
      using Self = infinit::overlay::KalimeroConfiguration;
      using Super = infinit::overlay::Configuration;

      KalimeroConfiguration();
      KalimeroConfiguration(elle::serialization::SerializerIn& input);
      ELLE_CLONABLE();
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::overlay::Overlay>
      make(std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) override;
    };
  }
}
