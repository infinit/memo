#include <infinit/overlay/kelips/Kelips.hh>

#include <algorithm>
#include <random>
#include <vector>
#include <utility>
#include <unordered_map>

#include <boost/filesystem.hpp>

#include <elle/bench.hh>
#include <elle/network/Interface.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>
#include <elle/serialization/json.hh>
#include <elle/utils.hh>

#include <cryptography/SecretKey.hh>
#include <cryptography/Error.hh>
#include <cryptography/hash.hh>
#include <cryptography/random.hh>

#include <reactor/Barrier.hh>
#include <reactor/Scope.hh>
#include <reactor/exception.hh>
#include <reactor/network/buffer.hh>
#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <infinit/storage/Filesystem.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Passport.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh> // FIXME

ELLE_LOG_COMPONENT("infinit.overlay.kelips");

typedef elle::serialization::Binary Serializer;

namespace elle
{
  namespace kelips = infinit::overlay::kelips;

  namespace serialization
  {
    template<typename T>
    struct SerializeEndpoint
    {
      typedef elle::Buffer Type;
      static Type convert(T& ep)
      {
        Type res;
        auto addr = ep.address().to_v4().to_bytes();
        res.append(addr.data(), addr.size());
        unsigned short port = ep.port();
        res.append(&port, 2);
        return res;
      }

      static T convert(elle::Buffer& repr)
      {
        ELLE_ASSERT(repr.size() == 6);
        unsigned short port;
        memcpy(&port, &repr[4], 2);
        auto addr = boost::asio::ip::address_v4(
          std::array<unsigned char, 4>{{repr[0], repr[1], repr[2], repr[3]}});
        return T(addr, port);
      }
    };

    template<> struct Serialize<kelips::GossipEndpoint>
      : public SerializeEndpoint<kelips::GossipEndpoint>
    {};

    template<> struct Serialize<kelips::RpcEndpoint>
      : public SerializeEndpoint<kelips::RpcEndpoint>
    {};

    template<> struct Serialize<kelips::Time>
    {
      typedef uint64_t Type;
      static uint64_t convert(kelips::Time& t)
      {
        Type res = std::chrono::duration_cast<std::chrono::milliseconds>(
          t.time_since_epoch()).count();
        return res;
      }

      static kelips::Time convert(uint64_t repr)
      {
        return kelips::Time(std::chrono::milliseconds(repr));
      }
    };
  }
}

struct PrettyGossipEndpoint
{
  PrettyGossipEndpoint(
    infinit::overlay::kelips::GossipEndpoint const& e)
    : _repr(e.address().to_string() + ":" + std::to_string(e.port()))
  {}

  PrettyGossipEndpoint(elle::serialization::Serializer& input)
  {
    this->serialize(input);
  }

  PrettyGossipEndpoint(std::string repr)
    : _repr(std::move(repr))
  {}

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize_forward(this->_repr);
  }

  operator infinit::overlay::kelips::GossipEndpoint()
  {
    size_t sep = this->_repr.find_first_of(':');
    auto a = boost::asio::ip::address::from_string(this->_repr.substr(0, sep));
    int p = std::stoi(this->_repr.substr(sep + 1));
    return infinit::overlay::kelips::GossipEndpoint(a, p);
  }

  ELLE_ATTRIBUTE_R(std::string, repr);
};


static void retry_forever(elle::Duration start_delay, elle::Duration max_delay,
                          std::string action_name,
                          std::function<void()> action)
{
  elle::Duration delay = start_delay;
  while (true)
  {
    try
    {
      action();
      return;
    }
    catch (elle::Exception const& e)
    {
      ELLE_WARN("%s: execption %s", action_name, e.what());
      delay = std::min(delay * 2, max_delay);
      reactor::sleep(delay);
    }
  }
}
namespace std
{
  namespace chrono
  {
    std::ostream& operator << (std::ostream&o,
                               time_point<std::chrono::system_clock> const& t)
    {
      return o << std::chrono::duration_cast<std::chrono::milliseconds>(
        t.time_since_epoch()).count();
    }
  }
}


namespace infinit
{
  namespace overlay
  {
    namespace kelips
    {

      static inline
      Time
      now()
      {
        return std::chrono::system_clock::now();
      }

      template<typename E1, typename E2>
      void
      endpoint_to_endpoint(E1 const& src, E2& dst)
      {
        dst = E2(src.address(), src.port());
      }

      RpcEndpoint e2e(GossipEndpoint const& src)
      {
        RpcEndpoint res;
        endpoint_to_endpoint(src, res);
        return res;
      }
      GossipEndpoint e2e(RpcEndpoint const& src)
      {
        GossipEndpoint res;
        endpoint_to_endpoint(src, res);
        return res;
      }

      static
      void
      endpoints_update(std::vector<TimedEndpoint>& endpoints, GossipEndpoint entry,
                       Time t = now())
      {
        auto hit = std::find_if(endpoints.begin(), endpoints.end(),
            [&](TimedEndpoint const& e) { return e.first == entry;});
        if (hit == endpoints.end())
          endpoints.push_back(TimedEndpoint(entry, now()));
        else
          hit->second = std::max(t, hit->second);
      }

      static
      void
      endpoints_update(std::vector<TimedEndpoint>& endpoints,
                       const std::vector<TimedEndpoint>& src)
      {
        for (auto const& te: src)
          endpoints_update(endpoints, te.first, te.second);
      }

      static
      void
      endpoints_cleanup(std::vector<TimedEndpoint>& endpoints, Time deadline)
      {
        for (unsigned i=0; i<endpoints.size(); ++i)
        {
          if (endpoints[i].second < deadline)
          {
            std::swap(endpoints[i], endpoints[endpoints.size()-1]);
            endpoints.pop_back();
            --i;
          }
        }
      }

      static
      Time
      endpoints_max(std::vector<TimedEndpoint> const& endpoints)
      {
        return std::max_element(endpoints.begin(), endpoints.end(),
          [](TimedEndpoint const& a, TimedEndpoint const& b) {
            return a.second < b.second;
          })->second;
      }

      static
      std::vector<RpcEndpoint>
      endpoints_extract_convert(std::vector<TimedEndpoint> const& endpoints)
      {
        std::vector<RpcEndpoint> res;
        for (auto const& ep: endpoints)
          res.push_back(e2e(ep.first));
        return res;
      }

      static
      std::vector<GossipEndpoint>
      endpoints_extract(std::vector<TimedEndpoint> const& endpoints)
      {
        std::vector<GossipEndpoint> res;
        for (auto const& ep: endpoints)
          res.push_back(ep.first);
        return res;
      }

      static
      uint64_t
      serialize_time(const Time& t)
      {
        return elle::serialization::Serialize<Time>::convert(
          const_cast<Time&>(t));
      }

      static
      std::string
      key_hash(infinit::cryptography::SecretKey const& k)
      {
        auto hk = infinit::cryptography::hash(k.password(),
                                               infinit::cryptography::Oneway::sha256);
        std::string hkhex = elle::sprintf("%x", hk);
        return hkhex.substr(0,3) + hkhex.substr(hkhex.length()-3);
      }

      static
      void
      merge_peerlocations(std::vector<PeerLocation>& dst,
                          std::vector<PeerLocation>const& src)
      {
        for (auto const& r: src)
        {
          auto it = std::find_if(dst.begin(), dst.end(),
            [&](PeerLocation const& a)
            {
              return a.first == r.first;
            });
          if (it == dst.end())
            dst.push_back(r);
          else
          {
            for (auto const& ep: r.second)
              if (std::find(it->second.begin(), it->second.end(), ep) == it->second.end())
                it->second.push_back(ep);
          }
        }
      }
      namespace packet
      {
        template<typename T>
        elle::Buffer
        serialize(T const& packet)
        {
          ELLE_ASSERT(&packet);
          elle::Buffer buf;
          elle::IOStream stream(buf.ostreambuf());
          Serializer::SerializerOut output(stream, false);
          auto ptr = &(packet::Packet&)packet;
          output.serialize_forward(ptr);
          return buf;
        }

#define REGISTER(classname, type) \
        static const elle::serialization::Hierarchy<Packet>::   \
        Register<classname>                                     \
        _registerPacket##classname(type)

        struct Packet
          : public elle::serialization::VirtuallySerializable<false>
        {
          GossipEndpoint endpoint;
          Address sender;
          bool observer; // true if sender is observer (no storage)
          static constexpr char const* virtually_serializable_key = "type";
        };

        struct EncryptedPayload: public Packet
        {
          EncryptedPayload()
          {}

          EncryptedPayload(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("payload", payload);
          }

          std::unique_ptr<Packet>
          decrypt(infinit::cryptography::SecretKey const& k)
          {
            elle::Buffer plain = k.decipher(
              payload,
              infinit::cryptography::Cipher::aes256,
              infinit::cryptography::Mode::cbc,
              infinit::cryptography::Oneway::sha256);
            elle::IOStream stream(plain.istreambuf());
            Serializer::SerializerIn input(stream, false);
            std::unique_ptr<packet::Packet> packet;
            input.serialize_forward(packet);
            return packet;

          }

          void encrypt(infinit::cryptography::SecretKey const& k,
                       Packet const& p)
          {
            elle::Buffer plain = packet::serialize(p);
            payload = k.encipher(plain,
                                 infinit::cryptography::Cipher::aes256,
                                 infinit::cryptography::Mode::cbc,
                                 infinit::cryptography::Oneway::sha256);
          }
          elle::Buffer payload;
        };
        REGISTER(EncryptedPayload, "crypt");

        struct RequestKey: public Packet
        {
          RequestKey(infinit::model::doughnut::Passport p)
            : passport(p)
          {}

          RequestKey(elle::serialization::SerializerIn& input)
            : passport(input)
          {
            input.serialize("sender", sender);
            input.serialize("observer", observer);
            input.serialize("token", token);
            input.serialize("challenge", challenge);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            passport.serialize(s);
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("token", token);
            s.serialize("challenge", challenge);
          }

          infinit::model::doughnut::Passport passport;
          elle::Buffer token;
          elle::Buffer challenge;
        };
        REGISTER(RequestKey, "reqk");

        struct KeyReply: public Packet
        {
          KeyReply(infinit::model::doughnut::Passport p)
            : passport(p)
          {}

          KeyReply(elle::serialization::SerializerIn& input)
            : passport(input)
          {
            input.serialize("sender", sender);
            input.serialize("observer", observer);
            input.serialize("encrypted_key", encrypted_key);
            input.serialize("token", token);
            input.serialize("signed_challenge", signed_challenge);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            passport.serialize(s);
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("encrypted_key", encrypted_key);
            s.serialize("token", token);
            s.serialize("signed_challenge", signed_challenge);
          }

          elle::Buffer encrypted_key;
          infinit::model::doughnut::Passport passport;
          elle::Buffer token;
          elle::Buffer signed_challenge;
        };
        REGISTER(KeyReply, "repk");

        struct Ping: public Packet
        {
          Ping()
          {}

          Ping(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("endpoint", remote_endpoint);
          }

          GossipEndpoint remote_endpoint;
        };
        REGISTER(Ping, "ping");

        struct Pong: public Ping
        {
          Pong()
          {}
          Pong(elle::serialization::SerializerIn& input)
            : Ping(input)
          {}
        };
        REGISTER(Pong, "pong");

        struct BootstrapRequest: public Packet
        {
          BootstrapRequest()
          {}

          BootstrapRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
          }
        };
        REGISTER(BootstrapRequest, "bootstrapRequest");

        struct FileBootstrapRequest: public Packet
        {
          FileBootstrapRequest()
          {}

          FileBootstrapRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
          }
        };
        REGISTER(FileBootstrapRequest, "fileBootstrapRequest");

        struct Gossip: public Packet
        {
          Gossip()
          {}

          Gossip(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("contacts", contacts);
            s.serialize("files", files);
          }
          // address -> (last_seen, val)
          std::unordered_map<Address, std::vector<TimedEndpoint>> contacts;
          std::unordered_multimap<Address, std::pair<Time, Address>> files;
        };
        REGISTER(Gossip, "gossip");

        struct GetFileRequest: public Packet
        {
          GetFileRequest()
          {}

          GetFileRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("id", request_id);
            s.serialize("origin", originAddress);
            s.serialize("endpoint", originEndpoints);
            s.serialize("address", fileAddress);
            s.serialize("ttl", ttl);
            s.serialize("count", count);
            s.serialize("result", result);
            s.serialize("query_node", query_node);
          }

          int request_id;
          /// origin node
          Address originAddress;
          std::vector<GossipEndpoint> originEndpoints;
          /// file address requested
          Address fileAddress;
          int ttl;
          /// number of results we want
          int count;
          /// partial result
          std::vector<PeerLocation> result;
          /// Request is for a node and not a file
          bool query_node;
        };
        REGISTER(GetFileRequest, "get");

        struct GetFileReply: public Packet
        {
          GetFileReply()
          {}

          GetFileReply(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("id", request_id);
            s.serialize("origin", origin);
            s.serialize("address", fileAddress);
            s.serialize("result", result);
            s.serialize("ttl", ttl);
          }

          int request_id;
          /// node who created the request
          Address origin;
          Address fileAddress;
          int ttl;
          std::vector<PeerLocation> result;
        };
        REGISTER(GetFileReply, "getReply");

        struct PutFileRequest: public GetFileRequest
        {
          PutFileRequest()
          {}

          PutFileRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            GetFileRequest::serialize(s);
            s.serialize("insert_ttl", insert_ttl);
            s.serialize("insert_result", insert_result);
          }

          // Nodes already having the block go in GetFileRequest::result,
          // insert_result is for new peers accepting it
          std::vector<PeerLocation> insert_result;
          /// insert when this reaches 0
          int insert_ttl;
        };
        REGISTER(PutFileRequest, "put");

        struct PutFileReply: public Packet
        {
          PutFileReply()
          {}

          PutFileReply(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("id", request_id);
            s.serialize("origin", origin);
            s.serialize("address", fileAddress);
            s.serialize("results", results);
            s.serialize("insert_results", insert_results);
            s.serialize("ttl", ttl);
          }

          int request_id;
          /// node who created the request
          Address origin;
          Address fileAddress;
          int ttl;
          std::vector<PeerLocation> results;
          std::vector<PeerLocation> insert_results;
        };
        REGISTER(PutFileReply, "putReply");

#undef REGISTER
      }

      struct PendingRequest
      {
        std::vector<PeerLocation> result;
        std::vector<PeerLocation> insert_result;
        reactor::Barrier barrier;
        Time startTime;
      };

      template<typename C>
      typename C::iterator
      random_from(C& container, std::default_random_engine& gen)
      {
        if (container.empty())
          return container.end();
        std::uniform_int_distribution<> random(0, container.size()-1);
        int v = random(gen);
        auto it = container.begin();
        while (v--) ++it;
        return it;
      }

      template<typename C, typename G>
      C
      pick_n(C const& src, int count, G& generator)
      {
        C res;
        std::uniform_int_distribution<> random(0, src.size()-1);
        for (int i=0; i<count; ++i)
        {
          int v;
          do {
            v = random(generator);
          }
          while (std::find(res.begin(), res.end(), src[v])!= res.end());
          res.push_back(src[v]);
        }
        return res;
      }

      template<typename C, typename G>
      C
      remove_n(C const& src, int count, G& generator)
      {
        C res(src);
        for (int i=0; i<count; ++i)
        {
          std::uniform_int_distribution<> random(0, res.size()-1);
          int v = random(generator);
          std::swap(res[res.size()-1], res[v]);
          res.pop_back();
        }
        return res;
      }

      Node::Node(Configuration const& config,
                 model::Address node_id,
                 std::shared_ptr<Local> local,
                 infinit::model::doughnut::Doughnut* doughnut)
        : Overlay(doughnut, local, std::move(node_id))
        , _config(config)
        , _next_id(1)
        , _observer(!local)
        , _dropped_puts(0)
        , _dropped_gets(0)
        , _failed_puts(0)
      {
        _self = Address(this->node_id());
        if (!local)
          ELLE_LOG("Running in observer mode");
        _remotes_server.listen(0);
        ELLE_DEBUG("remotes server listening on %s", _remotes_server.local_endpoint());
        _rdv_host = elle::os::getenv("INFINIT_RDV", "rdv.infinit.sh:7890");
        _rdv_id = elle::sprintf("%x", this->_self);
        if (!_rdv_host.empty())
          this->_rdv_connect_thread.reset(
            new reactor::Thread(
              "rdv_connect",
              [this]
              {
                // The remotes_server does not accept incoming connections,
                // it is used to connect Remotes
                retry_forever(10_sec, 120_sec, "Remote RDV connect",
                              [&] {
                                this->_remotes_server.rdv_connect(
                                  this->_rdv_id + "_", this->_rdv_host, 120_sec);
                              });
              }));
        if (_config.bootstrap_nodes.empty())
        {
          ELLE_LOG("Filesystem running in bootstrap read/write mode.");
          _bootstraping.open();
        }
        else
        {
          ELLE_LOG("Filesystem is read-only until peers are reached");
          _bootstraping.close();
        }
        start();
        if (auto l = local)
        {
          l->on_fetch.connect(std::bind(&Node::fetch, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
          l->on_store.connect(std::bind(&Node::store, this,
                                          std::placeholders::_1,
                                        std::placeholders::_2));
          l->on_remove.connect(std::bind(&Node::remove, this,
                                         std::placeholders::_1));

          l->rpcs().add("kelips_fetch_state",
                        std::function<SerState ()>(
                          [this] ()
                          {
                            SerState res;
                            std::vector<GossipEndpoint> eps;
                            for (auto const& e: _local_endpoints)
                              eps.push_back(e.first);
                            res.first.insert(std::make_pair(_self, eps));
                            for (auto const& contacts: this->_state.contacts)
                              for (auto const& c: contacts)
                              {
                                std::vector<GossipEndpoint> eps;
                                for (auto const& e: c.second.endpoints)
                                  eps.push_back(e.first);
                                res.first.insert(std::make_pair(c.second.address, eps));
                              }
                            for (auto const& f: this->_state.files)
                              res.second.push_back(std::make_pair(f.second.address, f.second.home_node));
                            // OH THE UGLY HACK, we need a place to store our own address
                            res.second.push_back(std::make_pair(Address::null, _self));
                            return res;
                          }));
          this->_port = l->server_endpoint().port();
          for (auto const& itf: elle::network::Interface::get_map(
                 elle::network::Interface::Filter::only_up |
                 elle::network::Interface::Filter::no_loopback |
                 elle::network::Interface::Filter::no_autoip))
            if (itf.second.ipv4_address.size() > 0)
            {
              this->_local_endpoints.push_back(TimedEndpoint(GossipEndpoint(
                                                               boost::asio::ip::address::from_string(itf.second.ipv4_address),
                                                               _port), now()));
              ELLE_LOG("%s: listening on %s:%s",
                       *this, itf.second.ipv4_address, _port);
            }
          if (!this->_rdv_host.empty())
            this->_rdv_connect_thread_local.reset(
              new reactor::Thread(
                "rdv_connect",
                [this,l] {
                  retry_forever(10_sec, 120_sec, "RDV connect",
                                [&] {
                                  l->utp_server()->rdv_connect(
                                    this->_rdv_id, this->_rdv_host, 120_sec);
                                });
                }));
            else
              l->utp_server()->set_local_id(this->_rdv_id);
          reload_state(*l);
          this->engage();
        }
      }

      int
      Node::group_of(Address const& a)
      {
        auto address = a.value();
        unsigned int addr4 = address[0]
          + (address[1] << 8)
          + (address[2] << 16)
          + (address[3] << 24);
        return addr4 % _config.k;
      }

      Node::~Node()
      {
        ELLE_TRACE_SCOPE("%s: destroy", *this);
        _emitter_thread.reset();
        _listener_thread.reset();
        _pinger_thread.reset();
        _rdv_connect_thread.reset();
        _rdv_connect_thread_local.reset();
        _rdv_connect_gossip_thread.reset();
        // Terminate rdv threads
        this->_state.contacts.clear();
      }

      SerState Node::get_serstate(PeerLocation pl)
      {
        std::vector<GossipEndpoint> endpoints;
        for (auto const& ep: pl.second)
          endpoints.push_back(GossipEndpoint(ep.address(), ep.port()));
        using Protocol = infinit::model::doughnut::Local::Protocol;
        auto protocol = this->_config.rpc_protocol;
        if (protocol == Protocol::utp || protocol == Protocol::all)
        {
          try
          {
            ELLE_DEBUG("utp connect to %s", pl);
            std::string uid;
            if (pl.first != Address::null)
              uid = elle::sprintf("%x", pl.first);
            infinit::model::doughnut::Remote peer(
              elle::unconst(*this->doughnut()),
              pl.first,
              endpoints,
              uid,
              elle::unconst(this)->_remotes_server);
            peer.connect(5_sec);
            ELLE_DEBUG("utp connected");
            auto rpc = peer.make_rpc<SerState()>("kelips_fetch_state");
            return rpc();
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("%s: UTP connection failed with %s: %s", *this, endpoints, e);
          }
        }
        if (protocol == Protocol::tcp || protocol == Protocol::all)
        {
          if (!pl.second.empty())
          {
            try
            {
              infinit::model::doughnut::consensus::Paxos::RemotePeer peer(
                elle::unconst(*this->doughnut()),
                pl.first,
                pl.second.front());
              peer.connect(5_sec);
              auto rpc = peer.make_rpc<SerState()>("kelips_fetch_state");
              return rpc();
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("%s: TCP connection failed with %s: %s", *this,
                endpoints.front(), e);
            }
          }
        }
        throw elle::Error(
          elle::sprintf("%s: Failed to contact peer %s", *this, endpoints.front()));
      }

      void
      Node::onPacket(reactor::network::Buffer nbuf, GossipEndpoint source)
      {
        ELLE_DUMP("Received %s bytes packet from %s", nbuf.size(), source);
        elle::Buffer buf(nbuf.data()+8, nbuf.size()-8);
        static bool async = getenv("INFINIT_KELIPS_ASYNC");
        if (async)
        new reactor::Thread(elle::sprintf("process"),
          [=] { this->process(buf, source);}, true);
        else
          process(buf, source);
      }

      void
      Node::bootstrap(bool use_bootstrap_nodes)
      {
        ELLE_TRACE("starting bootstrap procedure");
        std::set<Address> scanned;
        std::set<PeerLocation> candidates;
        if (use_bootstrap_nodes)
          for (auto const& b: _config.bootstrap_nodes)
            candidates.insert(b);
        for (auto const& c: this->_state.contacts[_group])
          candidates.insert(PeerLocation(c.second.address,
            endpoints_extract_convert(c.second.endpoints)));
        while (!candidates.empty())
        {
          PeerLocation pl = *candidates.begin();
          candidates.erase(pl);
          ELLE_DEBUG("fetching %s, %s candidates remaining", pl, candidates.size());
          try
          {
            SerState res = get_serstate(pl);
            // ugly hack, embeded self address
            scanned.insert(res.second.back().second);
            res.second.pop_back();
            process_update(res);
            for (auto const&c: _state.contacts[_group])
            {
              if (!scanned.count(c.first))
              {
                candidates.insert(PeerLocation(c.first,
                  endpoints_extract_convert(c.second.endpoints)));
              }
            }
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("%s", e);
          }
        }
        ELLE_TRACE("scanned %s nodes", scanned.size());
        // FIXME: weed out over-duplicated blocks
      }

      void
      Node::engage()
      {
        ELLE_TRACE("bootstraping");
        if (!_observer)
          bootstrap(true);
        ELLE_TRACE("joining");
        _gossip.socket()->close();
        if (!this->local())
        {
          _gossip.bind(GossipEndpoint({}, _port));
          if (!_rdv_host.empty())
            _rdv_connect_gossip_thread.reset(
              new reactor::Thread(
                "rdv gossip connect", [this]
                {
                  std::string id = elle::sprintf("%x", _self);
                  std::string host = _rdv_host;
                  int port = 7890;
                  auto p = host.find_first_of(':');
                  if ( p!= host.npos)
                  {
                    port = std::stoi(host.substr(p+1));
                    host = host.substr(0, p);
                  }
                  retry_forever(10_sec, 120_sec, "RDV connect",
                                [&] {
                                  _gossip.rdv_connect(id, host, port, 120_sec);
                });
                }));
          else
          {
            std::string id = elle::sprintf("%x", _self);
            _gossip.set_local_id(id);
          }
        }

        ELLE_TRACE("%s: bound to udp, member of group %s", *this, _group);

        if (!this->local())
        {
          _listener_thread.reset(new reactor::Thread(
                                   "listener",
                                   std::bind(&Node::gossipListener, this)));
          ELLE_LOG("%s: listening on port %s",
            *this, _gossip.local_endpoint().port());
        }
        else
        {
          this->local()->utp_server()->socket()->register_reader(
            "KELIPSGS", std::bind(&Node::onPacket, this, std::placeholders::_1,
              std::placeholders::_2));
          ELLE_LOG("%s: listening on %s",
            *this, this->local()->utp_server()->local_endpoint());
        }
        if (!_observer)
        {
          this->_pinger_thread.reset(
            new reactor::Thread(
              "pinger", std::bind(&Node::pinger, this)));
          this->_emitter_thread.reset(
            new reactor::Thread(
              "emitter", std::bind(&Node::gossipEmitter, this)));
        }

        // Send a bootstrap request to bootstrap nodes and all
        // nodes in group
        packet::BootstrapRequest req;
        req.sender = _self;

        auto send_bootstrap = [&req, this](PeerLocation const& l)
        {
          if (l.first != Address::null)
          {
            std::vector<GossipEndpoint> eps;
            for (auto const& ep: l.second)
              eps.push_back(e2e(ep));
            Contact& c = get_or_make(l.first, false, eps);
            ELLE_TRACE("%s: sending bootstrap to node %s", *this, l);
            if (!_config.encrypt || _config.accept_plain)
              send(req, c);
            else
            {
              packet::RequestKey req(make_key_request());
              send(req, c);
              _pending_bootstrap_address.push_back(l.first);
            }
          }
          else
          {
            ELLE_TRACE("Sending bootstrap to node %s", l.second);
            if (!_config.encrypt || _config.accept_plain)
            {
              for (auto const& ep: l.second)
                send(req, e2e(ep), Address::null);
            }
            else
            {
              packet::RequestKey req(make_key_request());
              for (auto const& ep: l.second)
                send(req, e2e(ep), Address::null);
            }
            for (auto const& ep: l.second)
              _pending_bootstrap_endpoints.push_back(e2e(ep));
          }
        };
        for (auto const& e: _config.bootstrap_nodes)
        {
          send_bootstrap(e);
        }
        for (auto& c: _state.contacts[_group])
        {
          send_bootstrap(PeerLocation(c.second.address,
            endpoints_extract_convert(c.second.endpoints)));
        }
        if (_config.wait)
          wait(_config.wait);
        ELLE_TRACE("%s: node engaged", *this);
      }

      void Node::start()
      {
        _group = group_of(_self);
        _state.contacts.resize(_config.k);
        // If we are not an observer, we must wait for Local port information
        if (_observer)
        {
          ELLE_TRACE("%s: engage", *this);
          engage();
        }
      }

      void
      Node::setKey(Address const& a,
                   infinit::cryptography::SecretKey sk)
      {
        auto oldkey = getKey(a);
        if (oldkey)
        {
          ELLE_DEBUG("%s: overriding key %s -> %s  for %s",
            *this, key_hash(*oldkey), key_hash(sk), a);
          _keys.find(a)->second = std::move(sk);
        }
        else
        {
          _keys.insert(std::make_pair(a, std::move(sk)));
        }
      }

      infinit::cryptography::SecretKey*
      Node::getKey(Address const& a)
      {
        auto it = _keys.find(a);
        if (it != _keys.end())
          return &it->second;
        return nullptr;
      }

      void
      Node::send(packet::Packet& p, Contact& c)
      {
        ELLE_DUMP("send to contact %x", c.address);
        send(p, &c, nullptr, nullptr);
      }
      void
      Node::send(packet::Packet& p, GossipEndpoint ep, Address addr)
      {
        ELLE_DUMP("send to endpoint %s : %s", addr, ep);
        send(p, nullptr, &ep, &addr);
      }
      void
      Node::send(packet::Packet& p, Contact* c, GossipEndpoint* ep,
                 Address* addr)
      {
        //std::string ptype = elle::type_info(p).name();
        //std::cerr << ptype << std::endl;
        ELLE_ASSERT(c || ep);
        ELLE_ASSERT(c || addr);
        auto address = c ? c->address : *addr;
        p.observer = this->_observer;
        bool is_crypto = dynamic_cast<const packet::EncryptedPayload*>(&p)
        || dynamic_cast<const packet::RequestKey*>(&p)
        || dynamic_cast<const packet::KeyReply*>(&p);
        elle::Buffer b;
        bool send_key_request = false;
        auto key = getKey(address);
        if (is_crypto
          || !_config.encrypt
          || (!key && _config.accept_plain)
          )
        {
          b = packet::serialize(p);
          send_key_request = _config.encrypt && !is_crypto;
        }
        else
        {
          if (!key)
          {
            // FIXME queue packet
            ELLE_DEBUG("%s: dropping packet to %s: no key available",
                       *this, address);
            send_key_request = true;
          }
          else
          {
            packet::EncryptedPayload ep;
            ep.sender = p.sender;
            ep.observer = p.observer;
            {
              static elle::Bench decrypt("kelips.encrypt", 10_sec);
              elle::Bench::BenchScope bs(decrypt);
              ep.encrypt(*key, p);
            }
            b = packet::serialize(ep);
          }
        }
        if (send_key_request)
        {
          packet::RequestKey req(make_key_request());
          req.sender = _self;
          req.observer = this->_observer;
          send(req, c, ep, addr);
        }
        if (b.size() == 0)
          return;

        GossipEndpoint e;
        if (ep)
          e = *ep;
        else if (c)
        {
          if (c->validated_endpoint)
            e = c->validated_endpoint->first;
          else
          {
            if (!c->contacter)
            {
              ELLE_DEBUG("Running contacter on %s", address);
              c->contacter.reset(
                new reactor::Thread(
                  "contacter",
                  [this, address] { this->contact(address);}));
            }
            else
              ELLE_DEBUG("Contacter already running on %s", address);
            if (c->pending.size() < 5)
              c->pending.push_back(b);
            return;
          }
        }
        static elle::Bench bencher("kelips.packet_size", 5_sec);
        bencher.add(b.size());
        reactor::Lock l(_udp_send_mutex);
        static elle::Bench bench("kelips.send", 5_sec);
        elle::Bench::BenchScope bs(bench);
        ELLE_DUMP("%s: sending %s bytes packet to %s\n%s", *this, b.size(), e, b.string());
        b.size(b.size()+8);
        memmove(b.mutable_contents()+8, b.contents(), b.size()-8);
        memcpy(b.mutable_contents(), "KELIPSGS", 8);
        auto& sock = this->local() ? *this->local()->utp_server()->socket()
          : _gossip;
        static bool async = getenv("INFINIT_KELIPS_ASYNC_SEND");
        if (async)
        {
          std::shared_ptr<elle::Buffer> sbuf = std::make_shared<elle::Buffer>(std::move(b));
          sock.socket()->async_send_to(
            boost::asio::buffer(sbuf->contents(), sbuf->size()),
            e,
            [sbuf] (  const boost::system::error_code& error,
              std::size_t bytes_transferred) {}
            );
        }
        else
        {
          try
          {
            sock.send_to(reactor::network::Buffer(b.contents(), b.size()), e);
          }
          catch (reactor::network::Exception const&)
          { // FIXME: do something
          }
        }
      }

      void
      Node::process(elle::Buffer const& buf, GossipEndpoint source)
      {
        //deserialize
        std::unique_ptr<packet::Packet> packet;
        elle::IOStream stream(buf.istreambuf());
        Serializer::SerializerIn input(stream, false);
        try
        {
          input.serialize_forward(packet);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("%s: Failed to deserialize packet: %s", *this, e);
          ELLE_TRACE("%x", buf);
          return;
        }
        if (!packet)
        {
          ELLE_WARN("%s: Received message without payload from %s.",
            *this, source);
          return;
        }

        packet->endpoint = source;
        bool was_crypted = false;
        // First handle crypto related packets
        if (auto p = dynamic_cast<packet::EncryptedPayload*>(packet.get()))
        {
          auto key = getKey(packet->sender);
          bool failure = true;
          if (!key)
          {
            ELLE_WARN("%s: key unknown for %s : %s",
              *this, source, packet->sender);
          }
          else
          {
            try
            {
              std::unique_ptr<packet::Packet> plain;
              {
                static elle::Bench decrypt("kelips.decrypt", 10_sec);
                elle::Bench::BenchScope bs(decrypt);
                plain = p->decrypt(*key);
              }
              if (plain->sender != p->sender)
              {
                ELLE_WARN("%s: sender inconsistency in encrypted packet: %s != %s",
                  *this, p->sender, plain->sender);
                return;
              }
              packet = std::move(plain);
              packet->endpoint = source;
              failure = false;
              was_crypted = true;
            }
            catch (infinit::cryptography::Error const& e)
            {
              ELLE_DEBUG(
                "%s: decryption with %s from %s : %s failed: %s",
                *this, key_hash(*key), source, packet->sender, e.what());
            }
          }
          if (failure)
          { // send a key request
            ELLE_DEBUG("%s: sending key request to %s", *this, source);
            packet::RequestKey rk(make_key_request());
            elle::Buffer s = packet::serialize(rk);
            send(rk, source, p->sender);
            return;
          }
        } // EncryptedPayload
        if (auto p = dynamic_cast<packet::RequestKey*>(packet.get()))
        {
          ELLE_DEBUG("%s: processing key request from %s", *this, source);
          // validate passport
          bool ok = p->passport.verify(*doughnut()->owner());
          if (!ok)
          {
            ELLE_WARN("%s: failed to validate passport from %s : %s",
              *this, source, p->sender);
            return;
          }
          // sign the challenge with our passport
          auto signed_challenge = doughnut()->keys().k().sign(
            p->challenge,
            infinit::cryptography::rsa::Padding::pss,
            infinit::cryptography::Oneway::sha256);
          // generate key
          auto sk = infinit::cryptography::secretkey::generate(256);
          elle::Buffer password = sk.password();
          setKey(p->sender, std::move(sk));
          packet::KeyReply kr(doughnut()->passport());
          kr.sender = _self;
          kr.encrypted_key = p->passport.user().seal(
            password,
            infinit::cryptography::Cipher::aes256,
            infinit::cryptography::Mode::cbc);
          kr.token = std::move(p->token);
          kr.signed_challenge = std::move(signed_challenge);
          send(kr, source, p->sender);
          return;
        } // requestkey
        if (auto p = dynamic_cast<packet::KeyReply*>(packet.get()))
        {
          ELLE_DEBUG("%s: processing key reply from %s", *this, source);
          // validate passport
          bool ok = p->passport.verify(*doughnut()->owner());
          if (!ok)
          {
            ELLE_WARN("%s: failed to validate passport from %s : %s",
              *this, source, p->sender);
            return;
          }
          // validate challenge
          auto it = this->_challenges.find(p->token.string());
          if (it == this->_challenges.end())
          {
            ELLE_LOG("%s at %s: challenge token %x does not exist.",
                     p->sender, source, p->token);
            return;
          }
          auto& stored_challenge = it->second;

          ok = p->passport.user().verify(
            p->signed_challenge,
            stored_challenge,
            infinit::cryptography::rsa::Padding::pss,
            infinit::cryptography::Oneway::sha256);
          this->_challenges.erase(it);
          if (!ok)
          {
            ELLE_LOG("%s at %s: Challenge verification failed",
                     p->sender, source);
            return;
          }
          // extract the key cyphered with our passport
          elle::Buffer password = doughnut()->keys().k().open(
            p->encrypted_key,
            infinit::cryptography::Cipher::aes256,
            infinit::cryptography::Mode::cbc);
          infinit::cryptography::SecretKey sk(std::move(password));
          setKey(p->sender, std::move(sk));
          // Flush operations waiting on crypto ready
          bool bootstrap_requested = false;
          {
            auto it = std::find(_pending_bootstrap_endpoints.begin(),
              _pending_bootstrap_endpoints.end(),
              source);
            if (it != _pending_bootstrap_endpoints.end())
            {
              ELLE_DEBUG("%s: processing queued operation to %s", *this, source);
              *it = _pending_bootstrap_endpoints[_pending_bootstrap_endpoints.size() - 1];
              _pending_bootstrap_endpoints.pop_back();
              bootstrap_requested = true;
            }
          }
          {
            auto it = std::find(_pending_bootstrap_address.begin(),
              _pending_bootstrap_address.end(),
              p->sender);
            if (it != _pending_bootstrap_address.end())
            {
              *it = _pending_bootstrap_address[_pending_bootstrap_address.size() - 1];
              _pending_bootstrap_address.pop_back();
              bootstrap_requested = true;
            }
          }
          if (bootstrap_requested &&
            std::find(_bootstrap_requests_sent.begin(),
              _bootstrap_requests_sent.end(),
              p->sender) == _bootstrap_requests_sent.end())
          {
            _bootstrap_requests_sent.push_back(p->sender);
            packet::BootstrapRequest req;
            req.sender = _self;
            send(req, source, p->sender);
          }

          onContactSeen(packet->sender, source, packet->observer);
          return;
        } // keyreply
        if (!was_crypted && !_config.accept_plain)
        {
          ELLE_WARN("%s: rejecting plain packet from %s : %s",
            *this, source, packet->sender);
          return;
        }

        onContactSeen(packet->sender, source, packet->observer);
        // TRAP: some packets inherit from each other, so most specific ones
        // must be first
        #define CASE(type) \
        else if (packet::type* p = dynamic_cast<packet::type*>(packet.get()))
          if (false) {}
        CASE(Pong)
        {
          onPong(p);
        }
        CASE(Ping)
        {
          packet::Pong r;
          r.sender = _self;
          r.remote_endpoint = source;
          send(r, source, p->sender);
        }
        CASE(Gossip)
        onGossip(p);
        CASE(BootstrapRequest)
        onBootstrapRequest(p);
        CASE(FileBootstrapRequest)
        onFileBootstrapRequest(p);
        CASE(PutFileRequest)
        onPutFileRequest(p);
        CASE(PutFileReply)
        onPutFileReply(p);
        CASE(GetFileRequest)
        onGetFileRequest(p);
        CASE(GetFileReply)
        onGetFileReply(p);
        else
          ELLE_WARN("%s: Unknown packet type %s", *this, typeid(*p).name());
        #undef CASE
      };

      void
      Node::gossipListener()
      {
        elle::Buffer buf;
        while (true)
        {
          buf.size(5000);
          GossipEndpoint source;
          ELLE_DUMP("%s: receiving packet...", *this);
          int sz = _gossip.receive_from(reactor::network::Buffer(buf.mutable_contents(), buf.size()),
                                        source);
          buf.size(sz);
          ELLE_DUMP("%s: received %s bytes from %s:\n%s", *this, sz, source, buf.string());
          memmove(buf.mutable_contents(), buf.contents()+8, buf.size()-8);
          buf.size(sz - 8);
          ELLE_DUMP("%s: received %s bytes from %s:\n%s", *this, sz, source, buf.string());
          static int counter = 1;

          static bool async = getenv("INFINIT_KELIPS_ASYNC");
          if (async)
            new reactor::Thread(elle::sprintf("process %s", counter++),
              [buf, source, this] { this->process(buf, source);}, true);
          else
            process(buf, source);
        }
      }

      template<typename T, typename U, typename G, typename C>
      void
      filterAndInsert(std::vector<Address> files, int target_count,
                      std::unordered_map<Address, std::pair<Time, T>>& res,
                      C& data,
                      T U::*access,
                      G& gen)
      {
        if (signed(files.size()) > target_count)
        {
          if (target_count < signed(files.size()) - target_count)
            files = pick_n(files, target_count, gen);
          else
            files = remove_n(files, files.size() - target_count, gen);
        }
        for (auto const& f: files)
        {
          auto& fd = data.find(f)->second;
          res.insert(std::make_pair(f,
            std::make_pair(fd.last_seen, fd.*access)));
          fd.last_gossip = now();
          fd.gossip_count++;
        }
      }

      void
      Node::filterAndInsert(
        std::vector<Address> files, int target_count, int group,
        std::unordered_map<Address, std::vector<TimedEndpoint>>& res)
      {
        if (signed(files.size()) > target_count)
        {
          if (target_count < signed(files.size()) - target_count)
            files = pick_n(files, target_count, _gen);
          else
            files = remove_n(files, files.size() - target_count, _gen);
        }
        for (auto const& f: files)
        {
          auto& fd = _state.contacts[group].find(f)->second;
          res.insert(std::make_pair(f, fd.endpoints));
          fd.last_gossip = now();
          fd.gossip_count++;
        }
      }

      void
      Node::filterAndInsert(
        std::vector<Address> files, int target_count,
        std::unordered_map<Address, std::pair<Time, Address>>& res)
      {
        kelips::filterAndInsert(files, target_count, res, _state.files,
                                &File::home_node, _gen);
      }

      void
      filterAndInsert2(
        std::vector<Contact*> new_contacts, unsigned int max_new,
        std::unordered_map<Address,std::vector<TimedEndpoint>>& res,
        std::default_random_engine gen)
      {
        if (new_contacts.size() > max_new)
        {
          if (max_new < new_contacts.size() - max_new)
            new_contacts = pick_n(new_contacts, max_new, gen);
          else
            new_contacts = remove_n(new_contacts, new_contacts.size() - max_new, gen);
        }
        for (auto const& f: new_contacts)
        {
          res.insert(std::make_pair(f->address, f->endpoints));
          f->last_gossip = now();
          f->gossip_count++;
        }
      }

      std::unordered_map<Address, std::vector<TimedEndpoint>>
      Node::pickContacts()
      {
        std::unordered_map<Address, std::vector<TimedEndpoint>> res;
        // start with our own group
        unsigned int max_new = _config.gossip.contacts_group / 2;
        unsigned int max_old = _config.gossip.contacts_group / 2;
        // insert new contacts
        std::vector<Address> new_files;
        for (auto const& f: _state.contacts[_group])
        {
          if (f.second.gossip_count < _config.gossip.new_threshold)
            new_files.push_back(f.first);
        }
        filterAndInsert(new_files, max_new, _group, res);
        new_files.clear();
        // insert old contacts, for which we got a refresh but did not gossip about
        for (auto const& f: _state.contacts[_group])
        {
          auto last_seen = endpoints_max(f.second.endpoints);
          if (f.second.last_gossip < last_seen
            && now() - last_seen > std::chrono::milliseconds(_config.gossip.old_threshold_ms)
            && res.find(f.first) == res.end())
            new_files.push_back(f.first);
        }
        filterAndInsert(new_files, max_old, _group, res);
        // If there is still room, random entries
        if (res.size() < (unsigned)_config.gossip.contacts_group)
        {
          int n = _config.gossip.contacts_group - res.size();
          // Pick at random
          std::vector<Address> available;
          for (auto const& f: _state.contacts[_group])
          {
            if (res.find(f.first) == res.end())
              available.push_back(f.first);
          }
          filterAndInsert(available, n, _group, res);
        }
        int size0 = res.size();
        // And now, do the same thing for other contacts, argh
        max_new = _config.gossip.contacts_other / 2;
        max_old = _config.gossip.contacts_other / 2;
        // insert new contacts
        std::vector<Contact*> new_contacts;
        for (int g=0; g<_config.k; ++g)
        {
          if (g == _group)
            continue;
          for (auto& f: _state.contacts[g])
          {
            if (f.second.gossip_count < _config.gossip.new_threshold)
              new_contacts.push_back(&f.second);
          }
        }
        filterAndInsert2(new_contacts, max_new, res, _gen);
        // insert old contacts
        new_contacts.clear();
        for (int g=0; g<_config.k; ++g)
        {
          if (g == _group)
            continue;
          for (auto& f: _state.contacts[g])
          {
            auto last_seen = endpoints_max(f.second.endpoints);
            if (f.second.last_gossip < last_seen
              && now() - last_seen > std::chrono::milliseconds(_config.gossip.old_threshold_ms)
              && res.find(f.first) == res.end())
                new_contacts.push_back(&f.second);
          }
        }
        filterAndInsert2(new_contacts, max_old, res, _gen);
        // insert random contacts if there is room
        new_contacts.clear();
        if (res.size() - size0 < (unsigned)_config.gossip.contacts_group)
        {
          int n = _config.gossip.contacts_other - res.size() + size0;
          // Pick at random
          for (int g=0; g<_config.k; ++g)
          {
            if (g == _group)
              continue;
            for (auto& f: _state.contacts[g])
            {
              if (res.find(f.first) == res.end())
                new_contacts.push_back(&f.second);
            }
          }
          filterAndInsert2(new_contacts, n, res, _gen);
        }
        return res;
      }

      template<typename C, typename K, typename V>
      bool
      has(C const& c, K const& k, V const& v)
      {
        auto its = c.equal_range(k);
        return std::find_if(its.first, its.second,
          [&](decltype(*its.first) e) { return e.second.second == v;}) != its.second;
      }

      std::unordered_multimap<Address, std::pair<Time, Address>>
      Node::pickFiles()
      {
        static elle::Bench bencher("kelips.pickFiles", 10_sec);
        elle::Bench::BenchScope bench_scope(bencher);
        static elle::Bench bench_new_candidates("kelips.newCandidates", 10_sec);
        static elle::Bench bench_old_candidates("kelips.oldCandidates", 10_sec);
        auto current_time = now();
        std::unordered_multimap<Address, std::pair<Time, Address>> res;
        int max_new = _config.gossip.files / 2;
        int max_old = _config.gossip.files / 2 + (_config.gossip.files % 2);
        ELLE_ASSERT_EQ(max_new + max_old, _config.gossip.files);
        // update self file last seen, this will avoid us some ifs at other places
        int new_candidates = 0;
        int old_candidates = 0;
        for (auto& f: _state.files)
        {
          if (f.second.home_node == _self)
          {
            f.second.last_seen = current_time;
          }
          if (f.second.gossip_count < _config.gossip.new_threshold)
            new_candidates++;
          if (f.second.home_node == _self
            && ((current_time - f.second.last_gossip) > std::chrono::milliseconds(_config.gossip.old_threshold_ms)))
            old_candidates++;
        }
        bench_new_candidates.add(new_candidates);
        bench_old_candidates.add(old_candidates);
        if (new_candidates  >= max_new * 2)
        {
          // pick max_new indexes in 0..new_candidates
          std::uniform_int_distribution<> random(0, new_candidates-1);
          std::vector<int> indexes;
          for (int i=0; i<max_new; ++i)
          {
            int v = random(_gen);
            if (std::find(indexes.begin(), indexes.end(), v) != indexes.end())
              --i;
            else
              indexes.push_back(v);
          }
          std::sort(indexes.begin(), indexes.end());
          int ipos = 0;
          int idx = 0;
          for (auto& f: _state.files)
          {
            if (ipos >= signed(indexes.size()))
              break;
            if (f.second.gossip_count < _config.gossip.new_threshold)
            {
              if (idx == indexes[ipos])
              {
                res.insert(std::make_pair(f.first,
                  std::make_pair(f.second.last_seen, f.second.home_node)));
                ipos++;
              }
              ++idx;
            }
          }
          ELLE_ASSERT_EQ(max_new, signed(res.size()));
        }
        else
        {
          // insert new files
          std::vector<std::pair<Address, std::pair<Time, Address>>> new_files;
          for (auto const& f: _state.files)
          {
            if (f.second.gossip_count < _config.gossip.new_threshold)
            new_files.push_back(std::make_pair(f.first,
              std::make_pair(f.second.last_seen, f.second.home_node)));
          }
          if (signed(new_files.size()) > max_new)
          {
            if (max_new < signed(new_files.size()) - max_new)
              new_files = pick_n(new_files, max_new, _gen);
            else
              new_files = remove_n(new_files, new_files.size() - max_new, _gen);
          }
          for (auto const& nf: new_files)
          {
            res.insert(nf);
          }
        }
        if (old_candidates >= max_old * 2)
        {
          // pick max_new indexes in 0..new_candidates
          std::uniform_int_distribution<> random(0, old_candidates-1);
          std::vector<int> indexes;
          for (int i=0; i<max_old; ++i)
          {
            int v = random(_gen);
            if (std::find(indexes.begin(), indexes.end(), v) != indexes.end())
              --i;
            else
              indexes.push_back(v);
          }
          std::sort(indexes.begin(), indexes.end());
          int ipos = 0;
          int idx = 0;
          for (auto& f: _state.files)
          {
            if (ipos >= signed(indexes.size()))
              break;
            if (f.second.home_node == _self
              && ((current_time - f.second.last_gossip) > std::chrono::milliseconds(_config.gossip.old_threshold_ms)))
            {
              if (idx == indexes[ipos])
              {
                res.insert(std::make_pair(f.first,
                  std::make_pair(f.second.last_seen, f.second.home_node)));
                ipos++;
              }
              ++idx;
            }
          }
        }
        else
        {
          // insert old files, only our own for which we can update the last_seen value
          std::vector<std::pair<Address, std::pair<Time, Address>>> old_files;
          for (auto& f: _state.files)
          {
            if (f.second.home_node == _self
              && ((current_time - f.second.last_gossip) > std::chrono::milliseconds(_config.gossip.old_threshold_ms))
              && !has(res, f.first, f.second.home_node))
              old_files.push_back(std::make_pair(f.first, std::make_pair(f.second.last_seen, f.second.home_node)));
          }
          if (signed(old_files.size()) > max_old)
          {
            if (max_old < signed(old_files.size()) - max_old)
              old_files = pick_n(old_files, max_old, _gen);
            else
              old_files = remove_n(old_files, old_files.size() - max_old, _gen);
          }
          for (auto const& nf: old_files)
          {
            res.insert(nf);
          }
        }
        // Check if we have room for more files
        if (res.size() < (unsigned)_config.gossip.files)
        {
          int n = _config.gossip.files - res.size();
          // Pick at random
          std::vector<std::pair<Address, std::pair<Time, Address>>> available;
          for (auto const& f: _state.files)
          {
            if (!has(res, f.first, f.second.home_node))
              available.push_back(std::make_pair(f.first, std::make_pair(f.second.last_seen, f.second.home_node)));
          }
          if (available.size() > unsigned(n))
          {
            if (n < signed(available.size()) - n)
              available = pick_n(available, n, _gen);
            else
              available = remove_n(available, available.size() - n, _gen);
          }
          for (auto const& nf: available)
          {
            res.insert(nf);
          }
        }
        for (auto const& r: res)
        {
          auto its = _state.files.equal_range(r.first);
          auto it = std::find_if(its.first, its.second,
            [&](decltype(*its.first) e) { return r.second.second == e.second.home_node;});
          it->second.gossip_count++;
          it->second.last_gossip = current_time;
        }
        assert(res.size() == unsigned(_config.gossip.files) || res.size() == _state.files.size());
        return res;
      }

      void
      Node::gossipEmitter()
      {
        std::uniform_int_distribution<> random(0, _config.gossip.interval_ms);
        int v = random(_gen);
        reactor::sleep(boost::posix_time::milliseconds(v));
        packet::Gossip p;
        p.sender = _self;
        p.observer = _observer;
        while (true)
        {
          reactor::sleep(boost::posix_time::millisec(_config.gossip.interval_ms));
          p.contacts.clear();
          p.files.clear();
          p.contacts = pickContacts();
          elle::Buffer buf = serialize(p);
          auto targets = pickOutsideTargets();
          for (auto const& a: targets)
          {
            auto it = _state.contacts[group_of(a)].find(a);
            if (it != _state.contacts[group_of(a)].end())
              send(p, it->second);
          }
          // Add some files, just for group targets
          p.files = pickFiles();
          buf = serialize(p);
          targets = pickGroupTargets();
          if (p.files.size() && targets.empty())
            ELLE_TRACE("%s: have files but no group member known", *this);
          for (auto const& a: targets)
          {
            if (!p.files.empty())
              ELLE_DUMP("%s: info on %s files %s   %x %x", *this, p.files.size(),
                       serialize_time(p.files.begin()->second.first),
                       _self, p.files.begin()->second.second);
            auto it = _state.contacts[group_of(a)].find(a);
            if (it != _state.contacts[group_of(a)].end())
              send(p, it->second);
          }
        }
      }

      void
      Node::onContactSeen(Address addr, GossipEndpoint endpoint, bool observer)
      {
        // Drop self
        if (addr == _self)
          return;
        int g = group_of(addr);
        if (g == _group && !_bootstraping.opened())
        {
          ELLE_LOG("Peer found, write enabled");
          _bootstraping.open();
        }
        Contacts& target = observer? _state.observers : _state.contacts[g];
        auto it = target.find(addr);
        if (it == target.end())
        {
          if (observer || g == _group || signed(target.size()) < _config.max_other_contacts)
          {
            Contact contact{TimedEndpoint(endpoint, now()),
                            {TimedEndpoint(endpoint, now())},
                            addr, Duration(0), Time(), 0};
            ELLE_LOG("%s: register %s", *this, contact);
            target[addr] = std::move(contact);
          }
        }
        else
        {
          // reset validated endpoint
          if (it->second.validated_endpoint && it->second.validated_endpoint->first != endpoint)
          {
            ELLE_LOG("%s: change validated endpoint %s -> %s", *this,
                     it->second.validated_endpoint->first, endpoint);
          }
          it->second.validated_endpoint = TimedEndpoint(endpoint, now());
          // add/update to endpoint list
          endpoints_update(it->second.endpoints, endpoint);
        }
      }

      void
      Node::onPong(packet::Pong* p)
      {
        if (!(p->sender == _ping_target))
        {
          ELLE_WARN("%s: Pong from unexpected host: expected %x, got %x", *this, _ping_target,
            p->sender);
          return;
        }
        Duration d = now() - _ping_time;
        int g = group_of(p->sender);
        Contacts& target = _state.contacts[g];
        auto it = target.find(p->sender);
        if (it == target.end())
        {
        }
        else
        {
          it->second.rtt = d;
        }
        _ping_target = Address();
        _ping_barrier.open();
        GossipEndpoint endpoint = p->remote_endpoint;
        endpoints_update(_local_endpoints, endpoint);
        auto contact_timeout = std::chrono::milliseconds(_config.contact_timeout_ms);
        endpoints_cleanup(_local_endpoints, now() - contact_timeout);
      }

      void
      Node::onGossip(packet::Gossip* p)
      {
        ELLE_DUMP("%s: processing gossip from %s", *this, p->endpoint);
        int g = group_of(p->sender);
        if (g != _group && !p->files.empty())
          ELLE_WARN("%s: Received files from another group: %s at %s", *this, p->sender, p->endpoint);
        for (auto const& c: p->contacts)
        {
          if (c.first == _self)
            continue;
          int g = group_of(c.first);
          auto& target = _state.contacts[g];
          auto it = target.find(c.first);
          if (it == target.end())
          {
            if (g == _group || target.size() < (unsigned)_config.max_other_contacts)
              target[c.first] = Contact{{}, c.second, c.first, Duration(), Time(), 0};
          }
          else
            endpoints_update(it->second.endpoints, c.second);
        }
        if (g == _group)
        {
          for (auto const& f: p->files)
          {
            auto its = _state.files.equal_range(f.first);
            auto it = std::find_if(its.first, its.second,
              [&](decltype(*its.first) i) -> bool {
              return i.second.home_node == f.second.second;});
            if (it == its.second)
            {
              _state.files.insert(std::make_pair(f.first,
                File{f.first, f.second.second, f.second.first, Time(), 0}));
              ELLE_DUMP("%s: registering %x live since %s (%s)", *this,
                         f.first,
                         std::chrono::duration_cast<std::chrono::seconds>(now() - f.second.first).count(),
                         (now() - f.second.first).count());
            }
            else
            {
              ELLE_DUMP("%s: %s %s %s %x", *this,
                       it->second.last_seen < f.second.first,
                       serialize_time(it->second.last_seen),
                       serialize_time(f.second.first),
                       f.first);
              it->second.last_seen = std::max(it->second.last_seen, f.second.first);
            }
          }
        }
      }

      void
      Node::onFileBootstrapRequest(packet::FileBootstrapRequest* p)
      {
        packet::Gossip res;
        res.sender = _self;
        res.files = pickFiles();
        send(res, p->endpoint, p->sender);
      }

      void
      Node::onBootstrapRequest(packet::BootstrapRequest* p)
      {
        auto ep = p->endpoint;
        int g = group_of(p->sender);
        if (g == _group && !p->observer)
        {
          PeerLocation peer(p->sender, {e2e(ep)});
          new reactor::Thread("reverse bootstraper",
            [this, peer] {
              try
              {
                SerState state = get_serstate(peer);
                state.second.pop_back(); // pop remote address
                ELLE_DEBUG("%s: inserting serstate from %s", *this, peer);
                process_update(state);
              }
              catch (elle::Error const& e)
              {
                ELLE_WARN("Error processing bootstrap data: %s", e);
              }
            }, true);
        }

        packet::Gossip res;
        res.sender = _self;

        int group_count = _state.contacts[g].size();
        // Special case to avoid the randomized fetcher running for ever
        if (group_count <= _config.gossip.bootstrap_group_target + 5)
        {
          for (auto const& e: _state.contacts[g])
            res.contacts.insert(std::make_pair(e.first,
              e.second.endpoints));
        }
        else
        {
          std::uniform_int_distribution<> random(0, group_count-1);
          for (int i=0; i< _config.gossip.bootstrap_group_target; ++i)
          {
            int v = random(_gen);
            auto it = _state.contacts[g].begin();
            while(v--) ++it;
            if (res.contacts.find(it->first) != res.contacts.end())
              --i;
            else
              res.contacts[it->first] = it->second.endpoints;
          }
        }
        // Same thing for other groups
        int other_count = 0;
        for (auto const& e: _state.contacts)
        {
          if (&e != &_state.contacts[g])
            other_count += e.size();
        }
        if (other_count <= _config.gossip.bootstrap_other_target + 5)
        {
          for (unsigned int i=0; i< _state.contacts.size(); ++i)
          {
            if (i == (unsigned)g)
              continue;
            else
              for (auto const& e: _state.contacts[i])
                res.contacts.insert(std::make_pair(e.first, e.second.endpoints));
          }
        }
        else
        {
          std::uniform_int_distribution<> random(0, _config.k-2);
          for (int i=0; i< _config.gossip.bootstrap_other_target; ++i)
          {
            int group = random(_gen);
            if (group == g)
              group = _config.k-1;
            if (_state.contacts[group].empty())
            {
              --i;
              continue;
            }
            std::uniform_int_distribution<> random2(0, _state.contacts[group].size()-1);
            int v = random2(_gen);
            auto it = _state.contacts[group].begin();
            while(v--) ++it;
            if (res.contacts.find(it->first) != res.contacts.end())
              --i;
            else
              res.contacts[it->first] = it->second.endpoints;
          }
        }
        send(res, p->endpoint, p->sender);
      }

      void
      Node::addLocalResults(packet::GetFileRequest* p,
        reactor::yielder<PeerLocation>::type const* yield)
      {
        static elle::Bench nlocalhit("kelips.localhit", 10_sec);
        int nhit = 0;
        int fg = group_of(p->fileAddress);
        auto its = _state.files.equal_range(p->fileAddress);
        // Shuffle the match list
        std::vector<decltype(its.first)> iterators;
        for (auto it = its.first; it != its.second; ++it)
          iterators.push_back(it);
        std::shuffle(iterators.begin(), iterators.end(), _gen);
        for (auto iti = iterators.begin(); iti != iterators.end(); ++iti)
        {
          ++nhit;
          auto it = *iti;
          // Check if this one is already in results
          if (std::find_if(p->result.begin(), p->result.end(),
            [&](PeerLocation const& r) -> bool {
              return r.first == it->second.home_node;
            }) != p->result.end())
          continue;
          // find the corresponding endpoints
          std::vector<RpcEndpoint> endpoints;
          bool found = false;
          if (it->second.home_node == _self)
          {
            ELLE_DEBUG("%s: found self", *this);
            if (_local_endpoints.empty())
            {
              ELLE_TRACE("Endpoint yet unknown, assuming localhost");
              endpoints.push_back(RpcEndpoint(
                boost::asio::ip::address::from_string("127.0.0.1"),
                this->_port));
            }
            else
            {
              endpoints = endpoints_extract_convert(_local_endpoints);
            }
            found = true;
          }
          else
          {
            auto contact_it = _state.contacts[fg].find(it->second.home_node);
            if (contact_it != _state.contacts[fg].end())
            {
              ELLE_DEBUG("%s: found other", *this);
              endpoints = endpoints_extract_convert(contact_it->second.endpoints);
              found = true;
            }
            else
              ELLE_LOG("%s: have file but not node", *this);
          }
          if (!found)
            continue;
          PeerLocation res;
          res.first = it->second.home_node;
          res.second = endpoints;
          p->result.push_back(res);
          if (yield)
            (*yield)(res);
        }
        nlocalhit.add(nhit);
      }

      void
      Node::onGetFileRequest(packet::GetFileRequest* p)
      {
        ELLE_TRACE("%s: getFileRequest %s/%x %s/%s", *this, p->request_id, p->fileAddress,
                 p->result.size(), p->count);
        if (p->originEndpoints.empty())
          p->originEndpoints = {p->endpoint};
        int fg = group_of(p->fileAddress);
        if (p->query_node)
        {
          auto& target = _state.contacts[fg];
          auto it = target.find(p->fileAddress);
          if (it != target.end())
            p->result.push_back(PeerLocation(it->first,
              endpoints_extract_convert(it->second.endpoints)));
        }
        else if (fg == _group)
        {
          addLocalResults(p, nullptr);
        }
        if (p->result.size() >= unsigned(p->count)
          || _state.contacts[fg].empty())
        { // We got the full result or we cant forward, send reply
          packet::GetFileReply res;
          res.sender = _self;
          res.fileAddress = p->fileAddress;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          res.result = p->result;
          res.ttl = p->ttl;
          if (p->originAddress == _self)
            onGetFileReply(&res);
          else
          {
            Contact& c = get_or_make(p->originAddress, p->observer, p->originEndpoints);
            ELLE_TRACE("%s: replying to %s/%s", *this, p->originEndpoints, p->request_id);
            send(res, c);
          }
          // FIXME: should we route the reply back the same path?
          return;
        }
        ELLE_TRACE("%s: route %s", *this, p->ttl);
        // We don't have it, route the request,
        if (p->ttl == 0)
        {
          // FIXME not in initial protocol, but we cant distinguish get and put
          packet::GetFileReply res;
          res.sender = _self;
          res.fileAddress = p->fileAddress;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          res.result = p->result;
          res.ttl = 1;
          if (p->originAddress == _self)
            onGetFileReply(&res);
          else
          {
            Contact& c = get_or_make(p->originAddress, p->observer, p->originEndpoints);
            send(res, c);
          }
          _dropped_gets++;
          return;
        }
        p->ttl--;
        p->sender = _self;
        int count = _state.contacts[fg].size();
        if (count == 0)
          return;
        std::uniform_int_distribution<> random(0, count-1);
        int idx = random(_gen);
        auto it = _state.contacts[fg].begin();
        while (idx--) ++it;
        send(*p, it->second);
      }

      void
      Node::onGetFileReply(packet::GetFileReply* p)
      {
        ELLE_DEBUG("%s: got reply for %x: %s", *this, p->fileAddress, p->result);
        auto it = _pending_requests.find(p->request_id);
        if (it == _pending_requests.end())
        {
          ELLE_TRACE("%s: Unknown request id %s", *this, p->request_id);
          return;
        }
        ELLE_DEBUG("%s: unlocking waiter on response %s: %s", *this, p->request_id,
                   p->result);
        static elle::Bench stime = elle::Bench("kelips.GET_RTT", boost::posix_time::seconds(5));
        stime.add(std::chrono::duration_cast<std::chrono::microseconds>(
          (now() - it->second->startTime)).count());
        static elle::Bench shops = elle::Bench("kelips.GET_HOPS", boost::posix_time::seconds(5));
        shops.add(p->ttl);
        it->second->result = p->result;
        it->second->barrier.open();
        _pending_requests.erase(it);
      }

      static bool in_peerlocation(std::vector<PeerLocation> const& pl,
                                  Address addr)
      {
        return std::find_if(pl.begin(), pl.end(),
            [&] (PeerLocation const& r) {
              return r.first == addr;
            }) != pl.end();
      }
      void
      Node::onPutFileRequest(packet::PutFileRequest* p)
      {
        ELLE_TRACE("%s: putFileRequest %s %s %s %x", *this, p->ttl, p->insert_ttl,
                   p->result.size(), p->fileAddress);
        int fg = group_of(p->fileAddress);
        if (p->originEndpoints.empty())
          p->originEndpoints = {p->endpoint};
        addLocalResults(p, nullptr);
        // don't accept put requests until we know our endpoint
        // Accept the put locally if we know no other node
        if (fg == _group
          &&  ((p->insert_ttl == 0 && !_local_endpoints.empty())
              || _state.contacts[_group].empty()))
        {
          if (!in_peerlocation(p->result, _self)
            && !in_peerlocation(p->insert_result, _self))
          // check if we didn't already accept this file
          {
            // Check if we already have the block
            auto its = _state.files.equal_range(p->fileAddress);
            auto it = std::find_if(its.first, its.second,
              [&](decltype(*its.first) i) ->bool {
                return i.second.home_node == _self;
              });
            if (it == its.second)
            { // Nope, insert here
              // That makes us a home node for this address, but
              // wait until we get the RPC to store anything
              ELLE_DEBUG("%s: inserting in insert_result", *this);
              p->insert_result.push_back(std::make_pair(_self,
                endpoints_extract_convert(_local_endpoints)));
              _promised_files.push_back(p->fileAddress);
            }
            else
            {
              ELLE_ASSERT(!"Should have been handled by addLocalResults");
            }
          }
          else
            ELLE_DEBUG("%s: not inserting %x: already inserted", *this, p->fileAddress);
        }
        // Forward
        if (p->insert_ttl > 0)
          p->insert_ttl--;
        if (p->ttl == 0
          || p->count <= signed(p->result.size())
          || _state.contacts[_group].empty())
        {
          packet::PutFileReply res;
          res.sender = _self;
          res.request_id = p->request_id;
          res.origin = p->originAddress;
          res.results = p->result;
          res.insert_results = p->insert_result;
          res.ttl = p->ttl;
          if (p->originAddress == _self)
            onPutFileReply(&res);
          else
          {
            Contact& c = get_or_make(p->originAddress, p->observer,
                                     p->originEndpoints);
            send(res, c);
          }
          if (p->count > signed(p->result.size()))
          {
            ELLE_TRACE("%s: reporting failed putfile request for %x", *this, p->fileAddress);
            _dropped_puts++;
          }
          return;
        }
        // Forward the packet to an other node
        auto it = random_from(_state.contacts[fg], _gen);
        if (it == _state.contacts[fg].end())
          it = random_from(_state.contacts[_group], _gen);
        if (it == _state.contacts[_group].end())
        {
          ELLE_ERR("%s: No contact founds", *this);
          return;
        }
        p->sender = _self;
        p->ttl--;
        send(*p, it->second);
      }

      void
      Node::onPutFileReply(packet::PutFileReply* p)
      {
        ELLE_DEBUG("%s: got reply for %s: %s", *this, p->request_id, p->results);
        auto it = _pending_requests.find(p->request_id);
        if (it == _pending_requests.end())
        {
          ELLE_TRACE("%s: Unknown request id %s", *this, p->request_id);
          return;
        }
        static elle::Bench stime = elle::Bench("kelips.PUT_RTT", boost::posix_time::seconds(5));
        stime.add(std::chrono::duration_cast<std::chrono::microseconds>(
          (now() - it->second->startTime)).count());
        static elle::Bench shops = elle::Bench("kelips.PUT_HOPS", boost::posix_time::seconds(5));
        shops.add(p->ttl);
        ELLE_DEBUG("%s: unlocking waiter on response %s: %s", *this, p->request_id, p->results);
        it->second->result = p->results;
        it->second->insert_result = p->insert_results;
        it->second->barrier.open();
        _pending_requests.erase(it);
      }

      void
      Node::address(Address file,
                    infinit::overlay::Operation op,
                    int n,
                    std::function<void (PeerLocation)> yield)
      {
        if (op == infinit::overlay::OP_INSERT)
        {
          std::vector<PeerLocation> res = kelipsPut(file, n);
          for (auto r: res)
            yield(r);
        }
        else if (op == infinit::overlay::OP_INSERT_OR_UPDATE)
        {
          bool hit = false;
          kelipsGet(file, n, false, -1, false, [&](PeerLocation r) {
              hit = true;
              yield(r);
          });
          if (!hit)
          {
            ELLE_TRACE("%s: get failed on %x, trying put", *this, file);
            std::vector<PeerLocation> res = kelipsPut(file, n);
            for (auto e: res)
              yield(e);
          }
        }
        else
        {
          kelipsGet(file, n, op == infinit::overlay::OP_FETCH, -1, false, yield);
        }
      }

      void
      Node::kelipsGet(Address file, int n, bool local_override, int attempts,
                      bool query_node,
                      std::function <void(PeerLocation)> yield)
      {
        ELLE_TRACE_SCOPE("%s: get %s", *this, file);
        if (attempts == -1)
          attempts = _config.query_get_retries;
        auto f = [this,file,n,local_override, attempts, yield, query_node]() {
          std::set<Address> result_set;
          packet::GetFileRequest r;
          r.sender = _self;
          r.request_id = ++ _next_id;
          r.query_node = query_node;
          r.originAddress = _self;
          for (auto const& te: _local_endpoints)
            r.originEndpoints.push_back(te.first);
          r.fileAddress = file;
          r.ttl = _config.query_get_ttl;
          r.count = n;
          int fg = group_of(file);
          static elle::Bench bench_localresult("kelips.localresult", 10_sec);
          static elle::Bench bench_localbypass("kelips.localbypass", 10_sec);
          if (!query_node && fg == _group)
          {
            // check if we have it locally
            auto its = _state.files.equal_range(file);
            auto it_us = std::find_if(its.first, its.second,
              [&](std::pair<const infinit::model::Address, File> const& f) {
                return f.second.home_node == _self;
              });
            if (it_us != its.second && (n == 1 || local_override))
            {
              ELLE_DEBUG("get satifsfied locally");
              yield(PeerLocation(Address::null,
                {RpcEndpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                            this->_port)}));
              return;
            }
            // add result for our own file table
            addLocalResults(&r, &yield);
            for (auto const& e: r.result)
              result_set.insert(e.first);
            if (result_set.size() >= unsigned(n))
            { // Request completed locally
              ELLE_DEBUG("Driver exiting");
              bench_localresult.add(1);
              return;
            }
          }
          if (query_node)
          {
            auto& target = _state.contacts[fg];
            auto it = target.find(file);
            if (it != target.end())
            {
              yield(PeerLocation(it->first,
                endpoints_extract_convert(it->second.endpoints)));
              return;
            }
          }
          ELLE_TRACE("git did not complete locally (%s)", result_set.size());
          for (int i = 0; i < attempts; ++i)
          {
            packet::GetFileRequest req(r);
            req.request_id = ++this->_next_id;
            auto r = std::make_shared<PendingRequest>();
            r->startTime = now();
            r->barrier.close();
            auto ir =
              this->_pending_requests.insert(std::make_pair(req.request_id, r));
            ELLE_ASSERT(ir.second);
            // Select target node
            auto it = random_from(_state.contacts[fg], _gen);
            if (it == _state.contacts[fg].end())
              it = random_from(_state.contacts[_group], _gen);
            if (it == _state.contacts[_group].end())
            {
              ELLE_TRACE("no contact to forward GET to");
              continue;
            }
            ELLE_DEBUG("%s: get request %s(%s)", *this, i, req.request_id);
            send(req, it->second);
            reactor::wait(r->barrier,
              boost::posix_time::milliseconds(_config.query_timeout_ms));
            if (!r->barrier.opened())
              ELLE_LOG("%s: get request on %s timeout (try %s)",
                        *this, file, i);
            else
            {
              ELLE_DEBUG("request %s (%s) gave %s results",
                         i, req.request_id, r->result.size());
              for (auto const& e: r->result)
              {
                if (fg == _group && !query_node)
                { // oportunistically add the entry to our tables
                  auto its = _state.files.equal_range(file);
                  auto it_r = std::find_if(its.first, its.second,
                    [&](std::pair<const infinit::model::Address, File> const& f) {
                      return f.second.home_node == e.first;
                    });
                  if (it_r == its.second)
                  _state.files.insert(std::make_pair(file,
                    File{file, e.first, now(), Time(), 0}));
                }
                if (result_set.insert(e.first).second)
                  yield(PeerLocation{e.first, e.second});
              }
              if (signed(result_set.size()) >= n)
                break;
            }
          }
        };
        return f();
      }

      std::vector<PeerLocation>
      Node::kelipsPut(Address file, int n)
      {
        int fg = group_of(file);
        packet::PutFileRequest p;
        p.query_node = false;
        p.sender = _self;
        p.originAddress = _self;
        for (auto const& te: _local_endpoints)
          p.originEndpoints.push_back(te.first);
        p.fileAddress = file;
        p.ttl = _config.query_put_ttl;
        p.count = n;
        // If there is only two nodes, inserting is deterministic without the rand
        p.insert_ttl = _config.query_put_insert_ttl + (rand()%2);
        std::vector<PeerLocation> results;
        std::vector<PeerLocation> insert_results;
        for (int i = 0; i < _config.query_put_retries; ++i)
        {
          packet::PutFileRequest req = p;
          req.request_id = ++_next_id;
          auto r = std::make_shared<PendingRequest>();
          r->startTime = now();
          r->barrier.close();
          _pending_requests[req.request_id] = r;
          elle::Buffer buf = serialize(req);
          // Select target node
          auto it = random_from(_state.contacts[fg], _gen);
          if (it == _state.contacts[fg].end())
            it = random_from(_state.contacts[_group], _gen);
          if (it == _state.contacts[_group].end())
          {
            if (fg != _group || _observer)
              return {};
            // Bootstraping only: Store locally.
            if (_config.bootstrap_nodes.empty())
            {
              _promised_files.push_back(p.fileAddress);
              results.push_back(PeerLocation(Address::null, {RpcEndpoint(
                boost::asio::ip::address::from_string("127.0.0.1"),
              this->_port)}));
              return results;
            }
            else
              return results;
          }
          ELLE_DEBUG("%s: put request %s(%s)", *this, i, req.request_id);
          send(req, it->second);
          reactor::wait(r->barrier,
            boost::posix_time::milliseconds(_config.query_timeout_ms));
          if (!r->barrier.opened())
          {
            ELLE_LOG("%s: Timeout on PUT attempt %s", *this, i);
          }
          else
          {
            merge_peerlocations(results, r->result);
            merge_peerlocations(insert_results, r->insert_result);
            if (signed(results.size()) + signed(insert_results.size())  >= n)
            {
              // If we got a partial reply first, then a full reply, we
              // can have more results than asked for.
              break;
            }
          }
          ELLE_TRACE("%s: put failed, retry %s", *this, i);
          ++_failed_puts;
        }
        ELLE_TRACE("%s: got %s results and %s insert", *this, results.size(),
                   insert_results.size());
        while (signed(results.size()) < n && !insert_results.empty())
        {
          results.push_back(insert_results.back());
          insert_results.pop_back();
        }
        if (signed(results.size()) > n)
        {
          ELLE_WARN("Requested %s peers for %x, got %s", n, file, results.size());
          results.resize(n);
        }
        return results;
      }
      /* For node selection, the paper [5] in kelips recommand:
        proba = c*(d+1)^(-Dp)
        c: Normalizer
        d: distance to other node
        p: magic param in ]1,2[
        D: space dimension
        kelips use
        1/(d^2)
        We only consider nodes with an rtt value set
      */

      static
      std::vector<Address>
      pick(std::map<Address, Duration> candidates, int count,
           std::default_random_engine& gen)
      {
        std::vector<Address> res;
        if (unsigned(count) >= candidates.size())
        {
          for (auto const& e: candidates)
            res.push_back(e.first);
          return res;
        }
        typedef std::chrono::duration<int, std::ratio<1, 1000000>> US;
        // get average latency, will be used for those with no rtt information
        long total = 0;
        int okcount = 0;
        for (auto const& e: candidates)
        {
          if (!(e.second == Duration())) //compiler glitch on != : ambiguous overload
          {
            total += std::chrono::duration_cast<US>(e.second).count();
            okcount += 1;
          }
        }
        int avgLatency = okcount ? total / okcount : 1;
        // apply to the map to avoid ifs belows
        for (auto& e: candidates)
          if (e.second == Duration())
            e.second = std::chrono::microseconds(avgLatency);
        // get total weight
        double proba_sum = 0;
        for (auto const& e: candidates)
        {
          double v = std::chrono::duration_cast<US>(e.second).count();
          proba_sum += 1.0 / pow(v, 2);
        }
        // roll
        while (res.size() < unsigned(count))
        {
          std::uniform_real_distribution<double> distribution(0.0, proba_sum);
          double target = distribution(gen);
          double sum = 0;
          auto it = candidates.begin();
          while (it != candidates.end())
          {
            double v = std::chrono::duration_cast<US>(it->second).count();
            sum += 1.0 / pow(v, 2);
            if (sum >= target)
              break;
            ++it;
          }
          if (it == candidates.end())
          {
            ELLE_WARN("no target %s", proba_sum);
            continue;
          }
          res.push_back(it->first);
          double v = std::chrono::duration_cast<US>(it->second).count();
          proba_sum -= 1.0 / pow(v, 2);
          candidates.erase(it);
        }
        return res;
      }

      std::vector<Address>
      Node::pickOutsideTargets()
      {
        std::map<Address, int> group_of;
        std::map<Address, Duration> candidates;
        for (unsigned int i=0; i<_state.contacts.size(); ++i)
        {
          if (i == unsigned(_group))
            continue;
          for (auto const& c: _state.contacts[i])
          {
            candidates[c.first] = c.second.rtt;
            group_of[c.first] = i;
          }
        }
        std::vector<Address> addresses = pick(candidates, _config.gossip.other_target, _gen);
        return addresses;
      }

      std::vector<Address>
      Node::pickGroupTargets()
      {
        std::map<Address, Duration> candidates;
        for (auto const& e: _state.contacts[_group])
          candidates[e.first] = e.second.rtt;
        std::vector<Address> r = pick(candidates, _config.gossip.group_target, _gen);
        return r;
      }

      void
      Node::pinger()
      {
        std::uniform_int_distribution<> random(0, _config.ping_interval_ms);
        int v = random(_gen);
        reactor::sleep(boost::posix_time::milliseconds(v));
        while (true)
        {
          ELLE_DUMP("%s: sleep for %s ms", *this, _config.ping_interval_ms);
          reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
          cleanup();
          // some stats
          static elle::Bench n_files("kelips.file_count", 10_sec);
          n_files.add(_state.files.size());

          // pick a target
          Contact* target;
          int group;
          while (true)
          {
            std::uniform_int_distribution<> random(0, _config.k-1);
            group = random(_gen);
            if (_state.contacts[group].empty())
            {
              reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
              continue;
            }
            std::uniform_int_distribution<> random2(0, _state.contacts[group].size()-1);
            int v = random2(_gen);
            auto it = _state.contacts[group].begin();
            while(v--) ++it;
            target = &it->second;
            _ping_target = it->first;
            break;
          }
          packet::Ping p;
          p.sender = _self;
          _ping_time = now();
          ELLE_DUMP("%s: pinging %x", *this, _ping_target);
          send(p, *target);
          bool ok = reactor::wait(_ping_barrier,
                                  boost::posix_time::milliseconds(_config.ping_timeout_ms));
          if (ok)
            ELLE_DUMP("%s: got pong reply", *this);
          else
            ELLE_TRACE("%s: Ping timeout on %x", *this, _ping_target);
          // onPong did the job for us
          _ping_barrier.close();
        }
      }

      void
      Node::cleanup()
      {
        static elle::Bench bench("kelips.cleared_files", 10_sec);
        auto it = _state.files.begin();
        auto t = now();
        auto file_timeout = std::chrono::milliseconds(_config.file_timeout_ms);
        int cleared = 0;
        while (it != _state.files.end())
        {
          if (!(it->second.home_node == _self) &&
              t - it->second.last_seen > file_timeout)
          {
            ELLE_DUMP("%s: erase file %x", *this, it->first);
            it = _state.files.erase(it);
            ++cleared;
          }
          else
            ++it;
        }
        bench.add(cleared);
        auto contact_timeout = std::chrono::milliseconds(_config.contact_timeout_ms);
        for (auto& contacts: _state.contacts)
        {
          auto it = contacts.begin();
          while (it != contacts.end())
          {
            endpoints_cleanup(it->second.endpoints, now() - contact_timeout);
            if (it->second.endpoints.empty())
            {
              ELLE_LOG("%s: erase %s", *this, it->second);
              it = contacts.erase(it);
            }
            else
              ++it;
          }
        }

        int time_send_all = _state.files.size() / (_config.gossip.files/2 ) *  _config.gossip.interval_ms;
        ELLE_DUMP("time_send_all is %s", time_send_all);
        if (time_send_all >= _config.file_timeout_ms / 4)
        {
          ELLE_TRACE_SCOPE(
            "%s: too many files for configuration: "
            "files=%s, per packet=%s, interval=%s, timeout=%s",
            *this, _state.files.size(), _config.gossip.files,
            _config.gossip.interval_ms, _config.file_timeout_ms);
          if (_config.gossip.files < 20)
          {
            // Keep it so it fits in 'standard' MTU of +/- 1k
            _config.gossip.files = std::min(20, _config.gossip.files * 3 / 2);
            ELLE_DEBUG("Increasing files/packet to %s", _config.gossip.files);
          }
          else if (_config.gossip.interval_ms > 400)
          {
            _config.gossip.interval_ms =
              std::max(400, _config.gossip.interval_ms * 2 / 3);
            ELLE_DEBUG("Decreasing interval to %s", _config.gossip.interval_ms);
          }
          else
          {
            // We're assuming each node has roughly the same number of files, so
            // others will increase their timeout as we do.
            _config.file_timeout_ms =  _config.file_timeout_ms * 3 / 2;
            ELLE_DEBUG("Increasing timeout to %s", _config.file_timeout_ms);
          }
        }
      }

      void
      Node::fetch(Address address,
                  std::unique_ptr<infinit::model::blocks::Block> & b)
      {}

      void
      Node::store(infinit::model::blocks::Block const& block,
                  infinit::model::StoreMode mode)
      {
        auto its = _state.files.equal_range(block.address());
        if (std::find_if(its.first, its.second, [&](Files::value_type const& f) {
            return f.second.home_node == _self;
        }) == its.second)
          _state.files.insert(std::make_pair(block.address(),
            File{block.address(), _self, now(), Time(), 0}));
        auto itp = std::find(_promised_files.begin(), _promised_files.end(), block.address());
        if (itp != _promised_files.end())
        {
          std::swap(*itp, _promised_files.back());
          _promised_files.pop_back();
        }
      }

      void
      Node::remove(Address address)
      {
        auto its = _state.files.equal_range(address);
        for (auto it = its.first; it != its.second; ++it)
        {
          if (it->second.home_node == _self)
          {
            _state.files.erase(it);
            break;
          }
        }
      }

      Node::Member
      Node::make_peer(PeerLocation hosts)
      {
        static bool disable_cache = getenv("INFINIT_DISABLE_PEER_CACHE");
        static int cache_count = std::stoi(elle::os::getenv("INFINIT_PEER_CACHE_DUP", "1"));
        ELLE_TRACE("connecting to %s", hosts);
        if (hosts.first == _self || hosts.first == Address::null)
        {
          ELLE_TRACE("target is local");
          return this->local();
        }
        for (auto const& ep: hosts.second)
        {
          for (auto const& l: _local_endpoints)
            if (ep == e2e(l.first))
            {
              ELLE_TRACE("target is local");
              return this->local();
            }
        }
        if (!disable_cache)
        {
          auto it = _peer_cache.find(hosts.first);
          if (it != _peer_cache.end() && signed(it->second.size()) >= cache_count)
            return it->second[rand()%it->second.size()];
        }
        std::vector<GossipEndpoint> endpoints;
        for (auto const& ep: hosts.second)
          endpoints.push_back(GossipEndpoint(ep.address(), ep.port()));
        using Protocol = infinit::model::doughnut::Local::Protocol;
        auto protocol = this->_config.rpc_protocol;
        if (protocol == Protocol::utp || protocol == Protocol::all)
        {
          try
          {
            std::string uid;
            if (hosts.first != Address::null)
              uid = elle::sprintf("%x", hosts.first);
            auto res = Overlay::Member(
              new infinit::model::doughnut::consensus::Paxos::RemotePeer(
                elle::unconst(*this->doughnut()),
                hosts.first,
                endpoints,
                uid,
                elle::unconst(this)->_remotes_server));
            std::dynamic_pointer_cast<model::doughnut::Remote>(res)
              ->retry_connect(std::function<bool(model::doughnut::Remote&)>(
                std::bind(&Node::remote_retry_connect,
                          this, std::placeholders::_1,
                          uid)));
            if (!disable_cache)
              _peer_cache[hosts.first].push_back(res);
            return res;
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("%s: UTP connection failed with %s: %s", *this, hosts, e);
          }
        }
        if (protocol == Protocol::tcp || protocol == Protocol::all)
        {
          if (!hosts.second.empty())
          {
            try
            {
              // FIXME: don't always yield paxos
              return Overlay::Member(
                new infinit::model::doughnut::consensus::Paxos::RemotePeer(
                  elle::unconst(*this->doughnut()),
                  hosts.first,
                  hosts.second.front()));
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("%s: TCP connection failed with %s: %s", *this, hosts, e);
            }
          }
        }
        throw elle::Error(elle::sprintf("Failed to connect to %s", hosts));
      }

      bool
      Node::remote_retry_connect(model::doughnut::Remote& remote, std::string const& id)
      {
        if (id.empty())
          return false;
        // Perform a lookup for the node
        boost::optional<PeerLocation> result;
        auto address = Address::from_string(id.substr(2));
        kelipsGet(address, 1, false, -1, true, [&](PeerLocation p)
          {
            result = p;
          });
        if (!result)
          return false;
        std::vector<GossipEndpoint> ge;
        for (auto const& e: result->second)
          ge.push_back(e2e(e));
        remote.initiate_connect(ge, id, _remotes_server);
        return true;
      }

      reactor::Generator<Node::Member>
      Node::_lookup(infinit::model::Address address,
                    int n,
                    infinit::overlay::Operation op) const
      {
        if (op != infinit::overlay::Operation::OP_FETCH)
        {
          ELLE_TRACE("Waiting for bootstrap");
          reactor::wait(elle::unconst(this)->_bootstraping);
          ELLE_TRACE("bootstrap opened");
        }
        return reactor::generator<Node::Member>(
          [this, address, n, op] (reactor::Generator<Node::Member>::yielder const& yield)
        {
          std::function<void(PeerLocation)> handle = [&](PeerLocation hosts)
          {
            yield(elle::unconst(this)->make_peer(hosts));
          };
          elle::unconst(this)->address(address, op, n, handle);
        });
      }

      void
      Node::print(std::ostream& stream) const
      {
        stream << "Kelips(" <<
          (_local_endpoints.empty() ? GossipEndpoint() : _local_endpoints.front().first)
          << ')';
      }

      void
      Node::wait(int count)
      {
        ELLE_LOG("%s: waiting for %s nodes", *this, count);
        while (true)
        {
          int sum = 0;
          for (auto const& contacts: _state.contacts)
            sum += contacts.size();
          if (sum >= count)
            break;
          ELLE_LOG("%s: waiting for %s nodes, got %s", *this, count, sum);
          reactor::sleep(1_sec);
        }
        reactor::sleep(1_sec);
      }

      void
      Node::reload_state(Local& l)
      {
        auto keys = l.storage()->list();
        for (auto const& k: keys)
        {
          _state.files.insert(std::make_pair(k,
            File{k, _self, now(), now(), _config.gossip.new_threshold + 1}));
          //ELLE_DUMP("%s: reloaded %x", *this, k);
        }
      }

      void
      Node::process_update(SerState const& s)
      {
        for (auto const& c: s.first)
        {
          if (c.first == _self)
            continue;
          int g = group_of(c.first);
          Contacts& target = _state.contacts[g];
          auto it = target.find(c.first);
          if (it == target.end())
          {
            if (g == this->_group ||
                signed(target.size()) < this->_config.max_other_contacts)
            {
              Contact contact{{}, {}, c.first, Duration(0), Time(), 0};
              for (auto const& ep: c.second)
                contact.endpoints.push_back(TimedEndpoint(ep, now()));
              ELLE_LOG("%s: register %s", *this, contact);
              target[c.first] = std::move(contact);
            }
          }
          else
          {
            for (auto const& ep: c.second)
              endpoints_update(it->second.endpoints, ep);
          }
        }
        for (auto const& f: s.second)
        {
          if (group_of(f.first) != _group)
            continue;
          if (f.second == _self)
            continue;
          auto its = _state.files.equal_range(f.first);
          auto it = std::find_if(its.first, its.second,
            [&](decltype(*its.first) i) -> bool {
            return i.second.home_node == f.second;});
          if (it == its.second)
          {
            _state.files.insert(std::make_pair(
                                  f.first,
                                  File{f.first, f.second, now(), now(),
                                      this->_config.gossip.new_threshold + 1}));
          }
        }
      }

      void
      Node::contact(Address address)
      {
        auto id = elle::sprintf("%x", address);
        int g = group_of(address);
        Contacts* contacts;
        auto it = _state.contacts[g].find(address);
        if (it != _state.contacts[g].end())
          contacts = &_state.contacts[g];
        else
        {
          it = _state.observers.find(address);
          if (it == _state.observers.end())
          {
            ELLE_WARN("Address %s not found in contacts", address);
            return;
          }
          contacts = &_state.observers;
        }
        auto peers = endpoints_extract(it->second.endpoints);
        // this yields, thus invalidating the iterator
        ELLE_DEBUG("contacting %s on %s", id, peers);
        auto& rsock =
          this->local() ? *this->local()->utp_server()->socket() : _gossip;
        auto res = rsock.contact(id, peers);
        it = contacts->find(address);
        if (it == contacts->end())
          return;
        if (!it->second.validated_endpoint)
        {
          it->second.validated_endpoint = TimedEndpoint(res, now());
        }
        else
          res = it->second.validated_endpoint->first;
        std::vector<elle::Buffer> buf;
        std::swap(it->second.pending, buf);
        ELLE_DEBUG("flushing send buffer to %s on %s", id, res);
        for (auto& b: buf)
        {
          b.size(b.size()+8);
          memmove(b.mutable_contents()+8, b.contents(), b.size()-8);
          memcpy(b.mutable_contents(), "KELIPSGS", 8);
          auto& sock =
            this->local() ? *this->local()->utp_server()->socket() : _gossip;
          try
          {
            sock.send_to(reactor::network::Buffer(b.contents(), b.size()), res);
          }
          catch (reactor::network::Exception const&)
          { // FIXME: do something
          }
        }
      }

      Contact&
      Node::get_or_make(Address address, bool observer,
                        std::vector<GossipEndpoint> endpoints)
      {
        Contacts& target = observer ?
        _state.observers : _state.contacts[group_of(address)];
        auto it = target.find(address);
        if (it == target.end())
        {
          Contact c {{},  {}, address, Duration(), Time(), 0};
          for (auto const& ep: endpoints)
            c.endpoints.push_back(TimedEndpoint(ep, now()));
          it = target.insert(std::make_pair(address, std::move(c))).first;
        }
        return it->second;
      }

      Overlay::Member
      Node::_lookup_node(Address address)
      {
        if (address == _self)
          return this->local();
        boost::optional<PeerLocation> result;
        kelipsGet(address, 1, false, -1, true, [&](PeerLocation p)
          {
            result = p;
          });
        if (!result)
          throw elle::Error(elle::sprintf("Node %s not found", address));
        return make_peer(*result);
      }

      packet::RequestKey
      Node::make_key_request()
      {
        packet::RequestKey req(doughnut()->passport());
        req.sender = _self;
        req.token = infinit::cryptography::random::generate<elle::Buffer>(128);
        req.challenge = infinit::cryptography::random::generate<elle::Buffer>(128);
        _challenges.insert(std::make_pair(req.token.string(), req.challenge));
        ELLE_DEBUG("Storing challenge %x", req.token);
        return req;
      }

      elle::json::Json
      Node::query(std::string const& k, boost::optional<std::string> const& v)
      {
        elle::json::Object res;
        if (k == "protocol")
        {
          using Protocol = infinit::model::doughnut::Local::Protocol;
          if (!v)
            res["protocol"] = _config.rpc_protocol == Protocol::utp ?
          "utp" : (_config.rpc_protocol == Protocol::tcp ? "tcp" : "all");
          else
          {
            if (*v == "tcp")
              _config.rpc_protocol = Protocol::tcp;
            else if (*v == "utp")
              _config.rpc_protocol = Protocol::utp;
            else if (*v == "all")
              _config.rpc_protocol = Protocol::all;
            else
              throw elle::Error("Invalid protocol");
          }
        }
        if (k == "stats")
        {
          res["group"] = this->_group;
          for (int i = 0; i < signed(this->_state.contacts.size()); ++i)
          {
            auto const& group = this->_state.contacts[i];
            elle::json::Array contacts;
            for (auto const& contact: group)
            {
              auto last_seen = std::chrono::duration_cast<std::chrono::seconds>
              (std::chrono::system_clock::now() - endpoints_max(contact.second.endpoints));
              contacts.push_back(elle::json::Object{
                  {"address", elle::sprintf("%x", contact.second.address)},
                  {"validated_endpoint",
                    elle::sprintf("%s", (contact.second.validated_endpoint?
                    PrettyGossipEndpoint(contact.second.validated_endpoint->first)
                  : PrettyGossipEndpoint(GossipEndpoint())).repr())},
                  {"endpoints", elle::sprintf("%s", contact.second.endpoints.size())},
                  {"last_seen",
                  elle::sprintf("%ss", last_seen.count())},
              });
            }
            res[elle::sprintf("%s", i)] = elle::json::Object{
              {"contacts", std::move(contacts)}
            };
          }
          res["files"] = this->_state.files.size();
          res["dropped_puts"] = this->_dropped_puts;
          res["dropped_gets"] = this->_dropped_gets;
          res["failed_puts"] = this->_failed_puts;
          elle::json::Array rtts;
          for (auto const& c: _state.contacts[_group])
          rtts.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(c.second.rtt).count());
          res["ping_rtt"] = rtts;
        }
        if (k == "blockcount")
        {
          auto files = _state.files;
          std::vector<int> counts;
          std::set<Address> processed;
          for (auto const& f: files)
          {
            if (processed.count(f.first))
              continue;
            processed.insert(f.first);
            auto its = files.equal_range(f.first);
            int count=0;
            for (; its.first != its.second; ++count, ++its.first)
              ;
            if (signed(counts.size()) <= count)
              counts.resize(count + 1, 0);
            counts[count]++;
          }
          elle::json::Array ares;
          for (auto c: counts)
            ares.push_back(c);
          res["counts"] = ares;
        }
        if (k.substr(0, strlen("cachecheck.")) == "cachecheck.")
        {
          Address addr = Address::from_string(k.substr(strlen("cachecheck.")));
          int g = group_of(addr);
          std::vector<int> hits;
          hits.resize(1);
          for (auto& c: this->_state.contacts[g])
          {
            packet::GetFileRequest gf;
            gf.sender = _self;
            gf.request_id = ++ _next_id;
            gf.query_node = false;
            gf.originAddress = _self;
            for (auto const& te: _local_endpoints)
              gf.originEndpoints.push_back(te.first);
            gf.fileAddress = addr;
            gf.ttl = 0;
            gf.count = 10;
            auto r = std::make_shared<PendingRequest>();
            r->startTime = now();
            r->barrier.close();
            this->_pending_requests.insert(std::make_pair(gf.request_id, r));
            send(gf, c.second);
            if (!reactor::wait(r->barrier, 100_ms))
              ++hits[0];
            else
            {
              int nh = r->result.size();
              if (signed(hits.size()) <= nh)
                hits.resize(nh+1);
              hits[nh]++;
            }
          }
          elle::json::Array ares;
          for (auto c: hits)
            ares.push_back(c);
          res["counts"] = ares;
        }
        if (k.substr(0, strlen("scan.")) == "scan.")
        {
          int factor = std::stoi(k.substr(strlen("scan.")));
          std::vector<Address> to_scan;
          std::set<Address> processed;
          // get addresses with copy count < factor
          for (auto const& f: _state.files)
          {
            if (processed.count(f.first))
              continue;
            processed.insert(f.first);
            auto its = _state.files.equal_range(f.first);
            int count=0;
            for (; its.first != its.second; ++count, ++its.first)
              ;
            if (count < factor)
              to_scan.push_back(f.first);
          }
          std::vector<int> counts;
          auto scanner = [&]
          {
            while (!to_scan.empty())
            {
              Address addr = to_scan.back();
              to_scan.pop_back();
              std::vector<PeerLocation> res;
              kelipsGet(addr, factor, false, 3, false, [&](PeerLocation pl) {
                  res.push_back(pl);
              });
              if (counts.size() <= res.size())
                counts.resize(res.size()+1, 0);
              counts[res.size()]++;
            }
          };
          elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
          {
            for (int i=0; i<10; ++i)
              s.run_background("scanner", scanner);
            while (!reactor::wait(s, 10_sec))
              ELLE_TRACE("scanner: %s remaining", to_scan.size());
          };
          elle::json::Array ares;
          for (auto c: counts)
            ares.push_back(c);
          res["counts"] = ares;
        }
        if (k == "bootstrap")
        {
          bootstrap(false);
        }
        if (k == "gossip")
        {
          if (!v)
            return elle::sprintf("files per packet: %s,  interval: %s ms, timeout: %s",
              _config.gossip.files, _config.gossip.interval_ms, _config.file_timeout_ms);
          size_t s1 = v->find_first_of(',');
          size_t s2 = v->find_last_of(',');
          ELLE_ASSERT(s1 != s2 && s1 != std::string::npos);
          std::string fpp = v->substr(0, s1);
          std::string interval = v->substr(s1+1, s2-s1-1);
          std::string timeout = v->substr(s2+1);
          _config.gossip.files = std::stol(fpp);
          _config.gossip.interval_ms = std::stol(interval);
          _config.file_timeout_ms = std::stol(timeout);
        }
        if (k.substr(0,5) == "node.")
        {
          Address target = Address::from_string(k.substr(5));
          Overlay::Member n;
          try {
            n = lookup_node(target);
            res["status"] = "got it";
          }
          catch (elle::Error const& e)
          {
            res["status"] = std::string("failed: ") + e.what();
          }
        }
        return res;
      }

      std::ostream&
      operator << (std::ostream& output, Contact const& contact)
      {
        elle::fprintf(output, "contact %x", contact.address);
        return output;
      }

      Configuration::Configuration()
        : overlay::Configuration()
        , k(1)
        , max_other_contacts(6)
        , query_get_retries(30)
        , query_put_retries(12)
        , query_timeout_ms(1000)
        , query_get_ttl(10)
        , query_put_ttl(10)
        , query_put_insert_ttl(3)
        , contact_timeout_ms(120000)
        , file_timeout_ms(1200000)
        , ping_interval_ms(1000)
        , ping_timeout_ms(1000)
        , bootstrap_nodes()
        , wait(0)
        , encrypt(false)
        , accept_plain(true)
        , rpc_protocol(infinit::model::doughnut::Local::Protocol::all)
        , gossip()
      {}

      Configuration::Configuration(elle::serialization::SerializerIn& input)
        : overlay::Configuration()
      {
        this->serialize(input);
      }

      void
      Configuration::serialize(elle::serialization::Serializer& s)
      {
        overlay::Configuration::serialize(s);
        s.serialize("k", k);
        s.serialize("max_other_contacts", max_other_contacts);
        s.serialize("query_get_retries", query_get_retries);
        s.serialize("query_put_retries", query_put_retries);
        s.serialize("query_timeout_ms", query_timeout_ms);
        s.serialize("query_get_ttl", query_get_ttl);
        s.serialize("query_put_ttl", query_put_ttl);
        s.serialize("query_put_insert_ttl", query_put_insert_ttl);
        s.serialize("contact_timeout_ms", contact_timeout_ms);
        s.serialize("file_timeout_ms", file_timeout_ms);
        s.serialize("ping_interval_ms", ping_interval_ms);
        s.serialize("ping_timeout_ms", ping_timeout_ms);
        s.serialize("gossip", gossip);
        s.serialize("bootstrap_nodes", bootstrap_nodes);
        s.serialize("wait", wait);
        s.serialize("encrypt", encrypt);
        s.serialize("accept_plain", accept_plain);
        s.serialize("rpc_protocol", rpc_protocol);
      }

      GossipConfiguration::GossipConfiguration()
        : interval_ms(2000)
        , new_threshold(5)
        , old_threshold_ms(40000)
        , files(6)
        , contacts_group(3)
        , contacts_other(3)
        , group_target(3)
        , other_target(3)
        , bootstrap_group_target(12)
        , bootstrap_other_target(12)
      {}

      GossipConfiguration::GossipConfiguration(
        elle::serialization::SerializerIn& input)
      {
        this->serialize(input);
      }

      void
      GossipConfiguration::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("interval_ms", interval_ms);
        s.serialize("new_threshold", new_threshold);
        s.serialize("old_threshold_ms", old_threshold_ms);
        s.serialize("files", files);
        s.serialize("contacts_group", contacts_group);
        s.serialize("contacts_other", contacts_other);
        s.serialize("group_target", group_target);
        s.serialize("other_target", other_target);
        s.serialize("bootstrap_group_target", bootstrap_group_target);
        s.serialize("bootstrap_other_target", bootstrap_other_target);
      }

      std::unique_ptr<infinit::overlay::Overlay>
      Configuration::make(Address id,
                          NodeEndpoints const& hosts,
                          std::shared_ptr<model::doughnut::Local> local,
                          model::doughnut::Doughnut* dht)
      {
        for (auto const& host: hosts)
        {
          ELLE_LOG("processing %s", host);
          if (host.first == id)
            continue;
          PeerLocation pl;
          if (host.first == model::Address())
            pl.first = Address::null;
          else
            pl.first = Address(host.first);
          for (auto const& ep: host.second)
          {
            try
            {
              auto p = ep.find_first_of(':');
              if (p == ep.npos)
                throw std::runtime_error("missing ':'");
              auto addr = ep.substr(0, p);
              auto port = std::stoi(ep.substr(p+1));
              pl.second.push_back(RpcEndpoint(
                boost::asio::ip::address::from_string(addr), port));
            }
            catch(std::exception const& e)
            {
              ELLE_LOG("skipping malformed address: %s", ep);
            }
          }
          this->bootstrap_nodes.push_back(pl);
        }
        return elle::make_unique<Node>(
          *this, std::move(id), std::move(local), dht);
      }

      static const
      elle::serialization::Hierarchy<infinit::overlay::Configuration>::
      Register<Configuration> _registerKelipsOverlayConfig("kelips");
    }
  }
}
