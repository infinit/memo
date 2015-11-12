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
    public:
      Kalimero(model::doughnut::Doughnut* dht,
               model::Address node_id,
               std::shared_ptr<model::doughnut::Local> local);
      virtual
      reactor::Generator<Member>
      _lookup(model::Address address, int n, Operation op) const override;
      virtual
      Overlay::Member
      _lookup_node(model::Address address) override;
    };

    struct KalimeroConfiguration
      : public Configuration
    {
      KalimeroConfiguration();
      KalimeroConfiguration(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(model::Address id,
           NodeEndpoints const& hosts,
           std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) override;
    };
  }
}

#endif
