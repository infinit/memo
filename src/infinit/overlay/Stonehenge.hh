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
      struct Peer
      {
        model::Address id;
        struct Endpoint
        {
          std::string host;
          int port;
        };
        boost::optional<Endpoint> endpoint;
        Peer(model::Address id);
        Peer(model::Address id, Endpoint e);
      };
      typedef boost::asio::ip::tcp::endpoint Host;
      typedef std::vector<Peer> Peers;
      Stonehenge(model::Address node_id,
                 Peers hosts,
                 std::shared_ptr<model::doughnut::Local> local,
                 model::doughnut::Doughnut* doughnut);
      ELLE_ATTRIBUTE_R(Peers, peers);

    /*------.
    | Peers |
    `------*/
    protected:
      virtual
      void
      _discover(NodeEndpoints const& peers) override;

    /*-------.
    | Lookup |
    `-------*/
    protected:
      virtual
      reactor::Generator<std::ambivalent_ptr<model::doughnut::Peer>>
      _lookup(model::Address address,
              int n,
              Operation op) const override;
      virtual
      Overlay::WeakMember
      _lookup_node(model::Address address) override;

    private:
      Overlay::WeakMember
      _make_member(Peer const& p) const;
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
      make(model::Address id,
           NodeEndpoints const& hosts,
           std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) override;
    };
  }
}

#endif
