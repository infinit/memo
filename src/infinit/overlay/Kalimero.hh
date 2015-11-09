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
      Kalimero(model::Address node_id);
      virtual
      reactor::Generator<Member>
      _lookup(model::Address address, int n, Operation op) const override;
      virtual
      Overlay::Member
      _lookup_node(model::Address address) override;
      virtual
      void
      register_local(std::shared_ptr<model::doughnut::Local> local) override;
      ELLE_ATTRIBUTE_R(std::weak_ptr<model::doughnut::Local>, local);
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
           NodeEndpoints const& hosts, bool server,
           model::doughnut::Doughnut* doughnut) override;
    };
  }
}

#endif
