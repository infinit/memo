#pragma once

#include <infinit/overlay/Overlay.hh>
#include <infinit/symbols.hh>

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
      using Host = boost::asio::ip::tcp::endpoint;
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
      bool
      _discovered(model::Address id) override;

    /*-------.
    | Lookup |
    `-------*/
    protected:
      elle::reactor::Generator<WeakMember>
      _allocate(model::Address address, int n) const override;
      elle::reactor::Generator<std::ambivalent_ptr<model::doughnut::Peer>>
      _lookup(model::Address address,
              int n,
              bool fast) const override;
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
      type_name() const override;
      elle::json::Array
      peer_list() const override;
      elle::json::Object
      stats() const override;
    };

    struct StonehengeConfiguration
      : public Configuration
    {
      using Self = infinit::overlay::StonehengeConfiguration;
      using Super = infinit::overlay::Configuration;
      struct Peer
      {
        std::string host;
        int port;
        model::Address id;
        using Model = elle::das::Model<Peer,
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
