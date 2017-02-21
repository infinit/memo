#ifndef INFINIT_OVERLAY_KALIMERO_HH
# define INFINIT_OVERLAY_KALIMERO_HH

# include <infinit/overlay/Overlay.hh>

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
      reactor::Generator<WeakMember>
      _allocate(model::Address address, int n) const override;
      reactor::Generator<WeakMember>
      _lookup(model::Address address, int n, bool fast) const override;
      Overlay::WeakMember
      _lookup_node(model::Address address) const override;

    /*-----------.
    | Monitoring |
    `-----------*/
    public:
      std::string
      type_name() override;
      elle::json::Array
      peer_list() override;
      elle::json::Object
      stats() override;
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

#endif
