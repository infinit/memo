#pragma once

#include <elle/reactor/asio.hh>
#include <boost/filesystem.hpp>
#include <boost/operators.hpp>

#include <elle/attribute.hh>
#include <elle/flat-set.hh>

#include <infinit/model/Address.hh>

namespace infinit
{
  namespace model
  {

    /*-----------.
    | Endpoint.  |
    `-----------*/
    class Endpoint
      : public elle::Printable
      , private boost::totally_ordered<Endpoint>
    {
    public:
      using Self = Endpoint;
      Endpoint(boost::asio::ip::address address, int port);
      Endpoint(boost::asio::ip::tcp::endpoint ep);
      Endpoint(boost::asio::ip::udp::endpoint ep);
      Endpoint(const Endpoint& b) = default;
      Endpoint();
      boost::asio::ip::tcp::endpoint
      tcp() const;
      boost::asio::ip::udp::endpoint
      udp() const;
      void
      serialize(elle::serialization::Serializer& s);
      ELLE_ATTRIBUTE_R(boost::asio::ip::address, address);
      ELLE_ATTRIBUTE_R(int, port);

    public:
      /// Same address and port.
      friend
      bool operator == (Endpoint const& a, Endpoint const& b);
      /// Lexicographical on address then port.
      friend
      bool operator < (Endpoint const& a, Endpoint const& b);

      void
      print(std::ostream& stream) const override;
    };


    /// Combine hash on address and port.
    void
    hash_combine(std::size_t& seed, Endpoint const& endpoint);

    /// Hash on address and port.
    std::size_t
    hash_value(Endpoint const& endpoint);


    /*------------.
    | Endpoints.  |
    `------------*/
    class Endpoints
      : public boost::container::flat_set<Endpoint>
    {
    public:
      using Super = boost::container::flat_set<Endpoint>;
      using Super::Super;
      Endpoints();
      Endpoints(std::vector<Endpoint> const&);
      Endpoints(Super const&);
      Endpoints(std::vector<boost::asio::ip::udp::endpoint> const&);
      Endpoints(boost::asio::ip::udp::endpoint);
      Endpoints(std::string const& address, int port);
      Endpoints(std::string const& repr);

      /// Import overloads.
      using Super::insert;
      /// Insert new addresses.
      ///
      /// flat_set is using a very broad signature for emplace, we
      /// can't intercept it.
      ///
      /// @param repr A "hostname:port" string.
      void
      insert(std::string const& repr);
      std::vector<boost::asio::ip::tcp::endpoint>
      tcp() const;
      std::vector<boost::asio::ip::udp::endpoint>
      udp() const;
      /// Merge endpoints without duplicates.
      ///
      /// @return Whether any endpoint was added.
      bool
      merge(Endpoints const&);
    };

    Endpoints
    endpoints_from_file(boost::filesystem::path const& path);

    using EndpointsRefetcher =
      std::function<boost::optional<Endpoints> (Address)>;

    /*---------------.
    | NodeLocation.  |
    `---------------*/
    class NodeLocation
    {
    public:
      NodeLocation(Address id, Endpoints endpoints);
      NodeLocation(const NodeLocation& b) = default;
      ELLE_ATTRIBUTE_RW(Address, id);
      ELLE_ATTRIBUTE_RX(Endpoints, endpoints);
    };

    std::ostream&
    operator <<(std::ostream& output, NodeLocation const& loc);

    using NodeLocations = std::vector<NodeLocation>;
  }
}

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<infinit::model::NodeLocation>
    {
      using Type = std::pair<infinit::model::Address,
                             boost::container::flat_set<infinit::model::Endpoint>>;
      static
      Type
      convert(infinit::model::NodeLocation const& nl);
      static
      infinit::model::NodeLocation convert(Type const& repr);
    };
  }
}
