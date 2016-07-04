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
      boost::asio::ip::tcp::endpoint
      tcp() const;
      boost::asio::ip::udp::endpoint
      udp() const;
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
      std::vector<boost::asio::ip::tcp::endpoint>
      tcp() const;
      std::vector<boost::asio::ip::udp::endpoint>
      udp() const;
    };

    class NodeLocation
    {
    public:
      NodeLocation(Address id, Endpoints endpoints);
      ELLE_ATTRIBUTE_R(Address, id);
      ELLE_ATTRIBUTE_R(Endpoints, endpoints);
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
    struct Serialize<infinit::model::Endpoint>
    {
      typedef std::string Type;
      static
      std::string
      convert(infinit::model::Endpoint const& ep);
      static
      infinit::model::Endpoint convert(std::string const& repr);
    };
  }
}

#endif
