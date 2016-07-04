#include <infinit/model/Endpoints.hh>

namespace infinit
{
  namespace model
  {
    Endpoint::Endpoint(boost::asio::ip::address address,
                       int port)
      : _address(std::move(address))
      , _port(port)
    {}

    Endpoint::Endpoint(boost::asio::ip::tcp::endpoint ep)
      : _address(ep.address())
      , _port(ep.port())
    {}

    Endpoint::Endpoint(boost::asio::ip::udp::endpoint ep)
      : _address(ep.address())
      , _port(ep.port())
    {}

    Endpoint::Endpoint(std::string const& repr)
    {
      size_t sep = repr.find_last_of(':');
      if (sep == std::string::npos || sep == repr.length())
        elle::err("invalid endpoint: %s", repr);
      this->_address =
        boost::asio::ip::address::from_string(repr.substr(0, sep));
      this->_port = std::stoi(repr.substr(sep + 1));
    }

    boost::asio::ip::tcp::endpoint
    Endpoint::tcp() const
    {
      return boost::asio::ip::tcp::endpoint(this->_address, this->_port);
    }

    boost::asio::ip::udp::endpoint
    Endpoint::udp() const
    {
      return boost::asio::ip::udp::endpoint(this->_address, this->_port);
    }

    std::ostream&
    operator <<(std::ostream& output, Endpoint const& loc)
    {
      elle::fprintf(output, "%s:%s", loc.address(), loc.port());
      return output;
    }

    std::vector<boost::asio::ip::tcp::endpoint>
    Endpoints::tcp() const
    {
      std::vector<boost::asio::ip::tcp::endpoint> res;
      std::transform(this->begin(), this->end(), std::back_inserter(res),
                     [] (Endpoint const& e) { return e.tcp(); });
      return res;
    }

    std::vector<boost::asio::ip::udp::endpoint>
    Endpoints::udp() const
    {
      std::vector<boost::asio::ip::udp::endpoint> res;
      std::transform(this->begin(), this->end(), std::back_inserter(res),
                     [] (Endpoint const& e) { return e.udp(); });
      return res;
    }

    NodeLocation::NodeLocation(model::Address id, Endpoints endpoints)
      : _id(std::move(id))
      , _endpoints(std::move(endpoints))
    {}

    std::ostream&
    operator <<(std::ostream& output, NodeLocation const& loc)
    {
      if (loc.id() != Address::null)
        if (is_fixed(output))
          elle::fprintf(output, "peer %f (%s)", loc.id(), loc.endpoints());
        else
          elle::fprintf(output, "peer %s (%s)", loc.id(), loc.endpoints());
      else
        elle::fprintf(output, "unknown peer (%s)", loc.endpoints());
      return output;
    }
  }
}

namespace elle
{
  namespace serialization
  {
    std::string
    Serialize<infinit::model::Endpoint>::convert(
      infinit::model::Endpoint const& ep)
    {
      return ep.address().to_string() + ":" + std::to_string(ep.port());
    }

    infinit::model::Endpoint
    Serialize<infinit::model::Endpoint>::convert(std::string const& repr)
    {
      size_t sep = repr.find_last_of(':');
      auto addr = boost::asio::ip::address::from_string(repr.substr(0, sep));
      int port = std::stoi(repr.substr(sep + 1));
      return infinit::model::Endpoint(addr, port);
    }
  }
}
