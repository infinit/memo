#include <elle/functional.hh>
#include <elle/make-vector.hh>
#include <elle/os/environ.hh>

#include <elle/reactor/network/resolve.hh>

#include <memo/model/Endpoints.hh>

namespace boost
{
  namespace asio
  {
    namespace ip
    {
      /// See http://stackoverflow.com/a/22747097/1353549.
      void
      hash_combine(std::size_t& h, address const& v)
      {
        if (v.is_v4())
          elle::hash_combine(h, v.to_v4().to_ulong());
        else if (v.is_v6())
          elle::hash_combine(h, v.to_v6().to_bytes());
        else if (v.is_unspecified())
          // guaranteed to be random: chosen by fair dice roll
          elle::hash_combine(h, 0x4751301174351161ul);
        else
          elle::hash_combine(h, v.to_string());
      }

      std::size_t
      hash_value(boost::asio::ip::address const& address)
      {
        std::size_t res = 0;
        hash_combine(res, address);
        return res;
      }
    }
  }
}

namespace
{
  /// Whether IPv6 support is disabled.
  static bool ipv4_only = !elle::os::getenv("MEMO_NO_IPV6", "").empty();
}

namespace memo
{
  namespace model
  {
    namespace bfs = boost::filesystem;

    /*-----------.
    | Endpoint.  |
    `-----------*/

    Endpoint::Endpoint()
      : _address()
      , _port(0)
    {}

    Endpoint::Endpoint(boost::asio::ip::address address, int port)
      : _address(std::move(address))
      , _port(port)
    {}

    Endpoint::Endpoint(boost::asio::ip::tcp::endpoint ep)
      : Self{ep.address(), ep.port()}
    {}

    Endpoint::Endpoint(boost::asio::ip::udp::endpoint ep)
      : Self{ep.address(), ep.port()}
    {}

    boost::asio::ip::tcp::endpoint
    Endpoint::tcp() const
    {
      return {this->_address, static_cast<unsigned short>(this->_port)};
    }

    boost::asio::ip::udp::endpoint
    Endpoint::udp() const
    {
      return {this->_address, static_cast<unsigned short>(this->_port)};
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

    void
    Endpoint::print(std::ostream& output) const
    {
      elle::fprintf(output, "%s:%s", this->address(), this->port());
    }

    bool
    operator == (Endpoint const& a, Endpoint const& b)
    {
      return std::tie(a._address, a._port)
        == std::tie(b._address, b._port);
    }

    bool
    operator < (Endpoint const& a, Endpoint const& b)
    {
      return std::tie(a._address, a._port)
        < std::tie(b._address, b._port);
    }

    void
    hash_combine(std::size_t& h, Endpoint const& endpoint)
    {
      boost::hash_combine(h, endpoint.address());
      boost::hash_combine(h, endpoint.port());
    }

    std::size_t
    hash_value(Endpoint const& endpoint)
    {
      std::size_t res = 0;
      hash_combine(res, endpoint);
      return res;
    }

    /*------------.
    | Endpoints.  |
    `------------*/

    Endpoints::Endpoints()
    {}

    Endpoints::Endpoints(std::vector<Endpoint> const& eps)
      : Super{std::begin(eps), std::end(eps)}
    {}

    Endpoints::Endpoints(Super const& eps)
      : Super{eps}
    {}

    Endpoints::Endpoints(std::vector<boost::asio::ip::udp::endpoint> const& eps)
    {
      for (auto const& e: eps)
        this->emplace(e);
    }

    Endpoints::Endpoints(boost::asio::ip::udp::endpoint ep)
    {
      this->emplace(std::move(ep));
    }

    // FIXME: make insert generic and use it.
    Endpoints::Endpoints(std::string const& address, int port)
    {
      for (auto&& ep: elle::reactor::network::resolve_udp(address, port, ipv4_only))
        this->emplace(std::move(ep));
    }

    Endpoints::Endpoints(std::string const& repr)
    {
      this->insert(repr);
    }

    void
    Endpoints::insert(std::string const& repr)
    {
      for (auto&& ep: elle::reactor::network::resolve_udp_repr(repr, ipv4_only))
        this->emplace(std::move(ep));
    }

    std::vector<boost::asio::ip::tcp::endpoint>
    Endpoints::tcp() const
    {
      return elle::make_vector(*this,
                               [](Endpoint const& e) { return e.tcp(); });
    }

    std::vector<boost::asio::ip::udp::endpoint>
    Endpoints::udp() const
    {
      return elle::make_vector(*this,
                               [](Endpoint const& e) { return e.udp(); });
    }

    bool
    Endpoints::merge(Endpoints const& b)
    {
      bool res = false;
      for (auto const& e: b)
        if (this->find(e) == this->end())
        {
          this->emplace(e);
          res = true;
        }
      return res;
    }

    Endpoints
    endpoints_from_file(bfs::path const& path)
    {
      bfs::ifstream f;
      f.open(path);
      if (!f.good())
        elle::err("unable to open for reading: %s", path);
      auto res = Endpoints{};
      for (std::string line; std::getline(f, line); )
        if (!line.empty())
          res.insert(line);
      return res;
    }

    /*---------------.
    | NodeLocation.  |
    `---------------*/

    NodeLocation::NodeLocation(model::Address id, Endpoints endpoints)
      : _id(std::move(id))
      , _endpoints(std::move(endpoints))
    {}

    std::ostream&
    operator <<(std::ostream& output, NodeLocation const& loc)
    {
      if (!loc.id())
        elle::fprintf(output, "unknown peer (%s)", loc.endpoints());
      else if (is_fixed(output))
        elle::fprintf(output, "peer %f", loc.id());
      else
        elle::fprintf(output, "peer %s (%s)", loc.id(), loc.endpoints());
      return output;
    }
  }
}

namespace elle
{
  namespace serialization
  {
    auto
    Serialize<memo::model::NodeLocation>::convert(memo::model::NodeLocation const& nl)
      -> Type
    {
      return std::make_pair(nl.id(), nl.endpoints());
    }

    memo::model::NodeLocation
    Serialize<memo::model::NodeLocation>::convert(Type const& repr)
    {
      return {repr.first, memo::model::Endpoints(repr.second)};
    }
  }
}
