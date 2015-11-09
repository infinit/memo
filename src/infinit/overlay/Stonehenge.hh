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
      typedef std::vector<std::pair<Host, model::Address>> Peers;
      Stonehenge(model::Address node_id,
                 Peers hosts, model::doughnut::Doughnut* doughnut);
      ELLE_ATTRIBUTE_R(Peers, peers);

    /*-------.
    | Lookup |
    `-------*/
    protected:
      virtual
      reactor::Generator<Member>
      _lookup(model::Address address,
              int n,
              Operation op) const override;
      virtual
      Overlay::Member
      _lookup_node(model::Address address) override;
    };

    struct StonehengeConfiguration
      : public Configuration
    {
      struct Peer
      {
        std::string host;
        int port;
        model::Address id;
      };

      std::vector<Peer> peers;
      StonehengeConfiguration();
      StonehengeConfiguration(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(NodeEndpoints const& hosts, bool server,
           model::doughnut::Doughnut* doughnut) override;
    };
  }
}

#endif
