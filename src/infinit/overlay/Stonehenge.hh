#ifndef INFINIT_OVERLAY_STONEHENGE_HH
# define INFINIT_OVERLAY_STONEHENGE_HH

# include <infinit/overlay/Overlay.hh>
# include <infinit/symbols.hh>

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
      Stonehenge(NodeLocations hosts,
                 std::shared_ptr<model::doughnut::Local> local,
                 model::doughnut::Doughnut* doughnut);
      ELLE_ATTRIBUTE_R(NodeLocations, peers);

    /*------.
    | Peers |
    `------*/
    protected:
      void
      _discover(NodeLocations const& peers) override;

    /*-------.
    | Lookup |
    `-------*/
    protected:
      reactor::Generator<std::ambivalent_ptr<model::doughnut::Peer>>
      _lookup(model::Address address,
              int n,
              Operation op) const override;
      Overlay::WeakMember
      _lookup_node(model::Address address) const override;

    private:
      Overlay::WeakMember
      _make_member(NodeLocation const& p) const;

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

    struct StonehengeConfiguration
      : public Configuration
    {
      typedef StonehengeConfiguration Self;
      typedef Configuration Super;
      struct Peer
      {
        std::string host;
        int port;
        model::Address id;
        using Model = das::Model<Peer,
                                 decltype(elle::meta::list(symbols::host,
                                                           symbols::port,
                                                           symbols::id))>;
      };

      std::vector<Peer> peers;
      StonehengeConfiguration();
      StonehengeConfiguration(elle::serialization::SerializerIn& input);
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
