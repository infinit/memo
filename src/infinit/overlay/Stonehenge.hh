#ifndef INFINIT_OVERLAY_STONEHENGE_HH
# define INFINIT_OVERLAY_STONEHENGE_HH

# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    class Stonehenge
      : public Overlay
    {
    /*-------------.
    | Construction |
    `-------------*/
    public:
      typedef boost::asio::ip::tcp::endpoint Host;
      typedef std::vector<Host> Hosts;
      Stonehenge(Hosts hosts, model::doughnut::Doughnut* doughnut);
      ELLE_ATTRIBUTE_R(Hosts, hosts);

    /*-------.
    | Lookup |
    `-------*/
    protected:
      virtual
      Members
      _lookup(model::Address address,
              int n,
              Operation op) const override;
    };

    struct StonehengeConfiguration
      : public Configuration
    {
      std::vector<std::string> hosts;
      StonehengeConfiguration();
      StonehengeConfiguration(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s);
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(std::vector<std::string> const& hosts, bool server,
        model::doughnut::Doughnut* doughnut) override;
    };
  }
}

#endif
