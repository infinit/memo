#ifndef INFINIT_MODEL_DOUGHNUT_ENDPOINTS
# define INFINIT_MODEL_DOUGHNUT_ENDPOINTS

# include <boost/asio.hpp>

# include <elle/attribute.hh>

# include <infinit/model/Address.hh>

namespace infinit
{
  namespace model
  {
    class Endpoint
    {
    public:
      Endpoint(boost::asio::ip::address address,
               int port);
      Endpoint(boost::asio::ip::tcp::endpoint ep);
      Endpoint(boost::asio::ip::udp::endpoint ep);
      Endpoint(std::string const& repr);
      Endpoint(const Endpoint& b) = default;
      Endpoint();
      ELLE_SERIALIZE_CONSTRUCT_DECLARE(Endpoint);
      bool operator == (Endpoint const& b) const;
      boost::asio::ip::tcp::endpoint
      tcp() const;
      boost::asio::ip::udp::endpoint
      udp() const;
      ELLE_SERIALIZE_FRIEND_FOR(Endpoint);
      void
      serialize(elle::serialization::Serializer& s);
      ELLE_ATTRIBUTE_R(boost::asio::ip::address, address);
      ELLE_ATTRIBUTE_R(int, port);
    };

    std::ostream&
    operator <<(std::ostream& output, Endpoint const& loc);

    class Endpoints
      : public std::vector<Endpoint>
    {
    public:
      using std::vector<Endpoint>::vector;
      Endpoints(std::vector<Endpoint> const&);
      Endpoints();
      std::vector<boost::asio::ip::tcp::endpoint>
      tcp() const;
      std::vector<boost::asio::ip::udp::endpoint>
      udp() const;
    };

    class NodeLocation
    {
    public:
      NodeLocation(Address id, Endpoints endpoints);
      NodeLocation(const NodeLocation& b) = default;
      ELLE_ATTRIBUTE_R(Address, id);
      ELLE_ATTRIBUTE_RX(Endpoints, endpoints);
    };

    std::ostream&
    operator <<(std::ostream& output, NodeLocation const& loc);

    typedef std::vector<NodeLocation> NodeLocations;
  }
}

namespace elle
{
  namespace serialization
  {

    template<>
    struct Serialize<infinit::model::NodeLocation>
    {
      typedef std::pair<infinit::model::Address,
                        std::vector<infinit::model::Endpoint>> Type;
      static
      Type
      convert(infinit::model::NodeLocation const& nl);
      static
      infinit::model::NodeLocation convert(Type const& repr);
    };
  }
}

#endif
