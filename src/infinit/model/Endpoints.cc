#include <reactor/network/resolve.hh>

#include <infinit/model/Endpoints.hh>

namespace infinit
{
  namespace model
  {
    Endpoint::Endpoint()
      : _address()
      , _port(0)
    {}

    Endpoint::Endpoint(boost::asio::ip::address address,
                       int port)
      : _address(std::move(address))
      , _port(port)
    {}

    Endpoint::Endpoint(std::string const& address,
                       int port)
      : Endpoint(reactor::network::resolve_udp(address, std::to_string(port)))
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

    bool
    Endpoint::operator == (Endpoint const& b) const
    {
      return _address == b._address && _port == b._port;
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

    void
    Endpoint::serialize(elle::serialization::Serializer& s)
    {
      if (s.text())
      {
        std::string repr;
        if (s.out())
          repr = address().to_string() + ":" + std::to_string(port());
        s.serialize_forward(repr);
        if (s.in())
        {
          size_t sep = repr.find_last_of(':');
          this->_address = boost::asio::ip::address::from_string(repr.substr(0, sep));
          this->_port = std::stoi(repr.substr(sep + 1));
        }
      }
      else
      { // binary serialization
        elle::Buffer res;
        if (s.out())
        {
          if (address().is_v4())
          {
            auto addr = address().to_v4().to_bytes();
            res.append(addr.data(), addr.size());
          }
          else
          {
            auto addr = address().to_v6().to_bytes();
            res.append(addr.data(), addr.size());
          }
          unsigned short p = static_cast<unsigned short>(port());
          res.append(&p, 2);
        }
        s.serialize_forward(res);
        if (s.in())
        {
          ELLE_ASSERT(res.size() == 6 || res.size() == 18);
          if (res.size() == 6)
          {
            unsigned short port;
            memcpy(&port, &res[4], 2);
            auto addr = boost::asio::ip::address_v4(
              std::array<unsigned char, 4>{{res[0], res[1], res[2], res[3]}});
            this->_port = port;
            this->_address = addr;
          }
          else
          {
            unsigned short port;
            memcpy(&port, &res[16], 2);
            auto addr = boost::asio::ip::address_v6(
              std::array<unsigned char, 16>{{
                res[0], res[1], res[2], res[3],
                res[4], res[5], res[6], res[7],
                res[8], res[9], res[10], res[11],
                res[12], res[13], res[14], res[15],
              }});
            this->_port = port;
            this->_address = addr;
          }
        }
      }
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

    Endpoints::Endpoints(std::vector<Endpoint> const& ep)
    : std::vector<Endpoint>(ep)
    {}

    Endpoints::Endpoints()
    {}

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
    auto
    Serialize<infinit::model::NodeLocation>::convert(infinit::model::NodeLocation const& nl)
    -> Type
    {
      return std::make_pair(nl.id(), nl.endpoints());
    }
    infinit::model::NodeLocation
    Serialize<infinit::model::NodeLocation>::convert(Type const& repr)
    {
      return infinit::model::NodeLocation(repr.first,
                                          infinit::model::Endpoints(repr.second));
    }
  }
}
