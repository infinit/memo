#ifndef INFINIT_OVERLAY_KADEMLIA_HH
# define INFINIT_OVERLAY_KADEMLIA_HH


# include <infinit/overlay/Overlay.hh>
# include <elle/serialization/Serializer.hh>
# include <reactor/network/udp-socket.hh>
# include <reactor/Barrier.hh>
# include <infinit/model/doughnut/Local.hh>

namespace kademlia
{
  typedef boost::asio::ip::udp::endpoint Endpoint;
  typedef infinit::model::Address Address;
  struct PrettyEndpoint: public Endpoint
  {
    using Endpoint::Endpoint;
  };
  struct Configuration
  {
    Configuration() {}
    Configuration(elle::serialization::SerializerIn& input) {serialize(input);}
    void
    serialize(elle::serialization::Serializer& s);
    Address node_id;
    int port;
    std::vector<PrettyEndpoint> bootstrap_nodes;
    int wait;
  };
  class Kademlia
    : public infinit::overlay::Overlay
    , public infinit::model::doughnut::Local
  {
  public:
    Kademlia(Configuration const& config,
             std::unique_ptr<infinit::storage::Storage> storage);
    void store(infinit::model::blocks::Block const& block, infinit::model::StoreMode mode) override;
    void remove(Address address) override;
    std::unique_ptr<infinit::model::blocks::Block> fetch(Address address) const override;
  protected:
    virtual Overlay::Members _lookup(infinit::model::Address address,
                                     int n, infinit::overlay::Operation op)
                                  const override;
  public:
    void on_event(int event, unsigned char *info_hash, void *data, size_t data_len);
  private:
    void _loop();
    typedef infinit::model::doughnut::Local Local;
    typedef infinit::overlay::Overlay Overlay;
    std::unique_ptr<reactor::Thread> _looper;
    reactor::network::UDPSocket _socket;
    Configuration _config;
    struct PendingRequest {
      reactor::Barrier barrier;
      infinit::overlay::Operation op;
      int n;
      Overlay::Members result;
    };
    mutable std::map<std::string, std::shared_ptr<PendingRequest>> _requests;
  };
}


#endif