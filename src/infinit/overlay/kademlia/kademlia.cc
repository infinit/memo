#include <infinit/overlay/kademlia/kademlia.hh>

#include <elle/log.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>

#include <elle/json/exceptions.hh>

#include <cryptography/hash.hh>
#include <reactor/network/buffer.hh>
#include <reactor/network/exception.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>

#include <random>

ELLE_LOG_COMPONENT("infinit.overlay.kademlia");

typedef elle::serialization::Json Serializer;

static inline kademlia::Time now()
{
  return std::chrono::steady_clock::now();
}

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<kademlia::PrettyEndpoint>
    {
      typedef std::string Type;
      static std::string convert(kademlia::PrettyEndpoint& ep)
      {
        return ep.address().to_string() + ":" + std::to_string(ep.port());
      }
      static kademlia::PrettyEndpoint convert(std::string const& repr)
      {
        size_t sep = repr.find_first_of(':');
        auto addr = boost::asio::ip::address::from_string(repr.substr(0, sep));
        int port = std::stoi(repr.substr(sep + 1));
        return kademlia::PrettyEndpoint(addr, port);
      }
    };
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
    template<> struct Serialize<kademlia::Endpoint>
    : public SerializeEndpoint<kademlia::Endpoint>
    {};
    /*
    template<> struct Serialize<kelips::RpcEndpoint>
    : public SerializeEndpoint<kelips::RpcEndpoint>
    {};*/
  }
}

static std::default_random_engine gen;

namespace kademlia
{
  Configuration::Configuration()
    : port(0)
    , wait(1)
    , wait_ms(500)
    , address_size(40)
    , k(8)
    , alpha(3)
    , ping_interval_ms(1000)
    , refresh_interval_ms(10000)
    , storage_lifetime_ms(120000)
  {}


  namespace packet
  {
    #define REGISTER(classname, type) \
    static const elle::serialization::Hierarchy<Packet>:: \
    Register<classname>   \
    _registerPacket##classname(type)

    struct Packet
    : public elle::serialization::VirtuallySerializable
    {
      Endpoint endpoint; // remote endpoint, filled by recvfrom
      Address sender;
    };
    #define SER(a, b, e) s.serialize(BOOST_PP_STRINGIZE(e), e);
#define PACKET(name, ...)                                             \
      name() {}                                                           \
      name(elle::serialization::SerializerIn& input) {serialize(input);}  \
      void                                                                \
      serialize(elle::serialization::Serializer& s)                       \
      {                                                                   \
        BOOST_PP_SEQ_FOR_EACH(SER, _,                                     \
          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))                          \
      }

    struct Ping: public Packet
    {
      PACKET(Ping, sender, remote_endpoint);
      Endpoint remote_endpoint;
    };
    REGISTER(Ping, "ping");

    struct Pong: public Packet
    {
      PACKET(Pong, sender, remote_endpoint);
      Endpoint remote_endpoint;
    };
    REGISTER(Pong, "pong");

    struct FindNode: public Packet
    {
      PACKET(FindNode, sender, target, requestId);
      int requestId;
      Address target;
    };
    REGISTER(FindNode, "findN");
    struct FindNodeReply: public Packet
    {
      PACKET(FindNodeReply, sender, requestId, nodes);
      int requestId;
      std::unordered_map<Address, Endpoint> nodes;
    };
    REGISTER(FindNodeReply, "foundN");
    struct FindValue: public Packet
    {
      PACKET(FindValue, sender, target, requestId);
      int requestId;
      Address target;
    };
    REGISTER(FindValue, "findV");
    struct FindValueReply: public Packet
    {
      PACKET(FindValueReply, sender, requestId, nodes, results);
      int requestId;
      std::unordered_map<Address, Endpoint> nodes;
      std::vector<Endpoint> results;
    };
    REGISTER(FindValueReply, "foundV");
    struct Store: public Packet
    {
      PACKET(Store, sender, key, value);
      Address key;
      std::vector<Endpoint> value;
    };
    REGISTER(Store, "store");
    #undef REGISTER
    #undef SER
    #undef PACKET
  }

  template<typename E1, typename E2>
  void endpoint_to_endpoint(E1 const& src, E2& dst)
  {
    dst = E2(src.address(), src.port());
  }

  Kademlia::Kademlia(infinit::model::Address node_id,
                     Configuration const& config,
                     std::shared_ptr<Local> local,
                     infinit::model::doughnut::Doughnut* doughnut)
    : Overlay(doughnut, std::move(local), std::move(node_id))
    , _config(config)
  {
    srand(time(nullptr) + getpid());
    _self = _config.node_id;
    _routes.resize(_config.address_size);
    Address::Value v;
    memset(v, 0xFF, sizeof(Address::Value));
    for (int i = sizeof(v)-1; i>=0; --i)
    {
      for (int b=7; b>=0; --b)
      {
        if (_config.address_size <= i*8 + b )
          v[i] = v[i] & ~(1 << b);
        else
          goto done;
      }
    }
  done:
    _mask = Address(v);
    if (local)
    {
      local->on_fetch.connect(std::bind(&Kademlia::fetch, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
      local->on_store.connect(std::bind(&Kademlia::store, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
      local->on_remove.connect(std::bind(&Kademlia::remove, this,
                                         std::placeholders::_1));
      this->_port = local->server_endpoint().port();
      this->_config.port = this->_port;
    }
    this->_bootstrap();
    if (local)
      this->_reload(*local);
  }

  Kademlia::~Kademlia()
  {
    if (_looper)
      _looper->terminate_now();
    if (_pinger)
      _pinger->terminate_now();
    if (_refresher)
      _refresher->terminate_now();
    if (_cleaner)
      _cleaner->terminate_now();
    if (_republisher)
      _republisher->terminate_now();
  }

  void Kademlia::_bootstrap()
  {
    ELLE_TRACE("Using address mask %x, port %s", _mask, _config.port);
    _socket.socket()->close();
    _socket.bind(Endpoint({}, _config.port));

    _looper = elle::make_unique<reactor::Thread>("looper",
      [this] { this->_loop();});
    _pinger = elle::make_unique<reactor::Thread>("pinger",
      [this] { this->_ping();});
    _refresher = elle::make_unique<reactor::Thread>("refresher",
      [this] { this->_refresh();});
    _cleaner = elle::make_unique<reactor::Thread>("cleaner",
      [this] { this->_cleanup();});
    _republisher = elle::make_unique<reactor::Thread>("republisher",
      [this] { this->_republish();});
    for (auto const& ep: _config.bootstrap_nodes)
    {
      packet::Ping p;
      p.sender = _self;
      p.remote_endpoint = ep;
      send(elle::serialization::json::serialize(&p), ep);
    }
    if (_config.wait)
    {
      while (true)
      {
        int n = 0;
        for(auto const& e: _routes)
          n += e.size();
        ELLE_TRACE("%s: Waiting for %s nodes, got %s", *this, _config.wait, n);
        if (n >= _config.wait)
          break;
        reactor::sleep(1_sec);
      }
    }
    reactor::sleep(boost::posix_time::milliseconds(_config.wait_ms));
    ELLE_LOG("%s: exiting ctor", *this);
  }

  void Kademlia::_loop()
  {
    elle::Buffer buf;
    while (true)
    {
      buf.size(5000);
      int n = 0;
      for(auto const& e: _routes)
        n += e.size();
      ELLE_TRACE("%s: knows %s nodes and %s hashes,", *this, n, _storage.size());
      Endpoint ep;
      size_t sz = _socket.receive_from(
          reactor::network::Buffer(buf.mutable_contents(), buf.size()),
          ep);
      if (sz == 0)
      {
        ELLE_WARN("%s: empty packet received", *this);
        continue;
      }
      buf.size(sz);
      std::unique_ptr<packet::Packet> packet;
      elle::IOStream stream(buf.istreambuf());
      try
      {
        Serializer::SerializerIn input(stream, false);
        input.serialize_forward(packet);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("%s: Failed to deserialize packet from %s: %s",
                  *this, ep, e);
        ELLE_WARN("%s", buf.string());
        continue;
      }
      catch(elle::json::ParseError const& e)
      {
        ELLE_WARN("%s: json parse error from %s: %s", *this, ep, e);
        ELLE_WARN("%s", buf.string());
        continue;
      }
      packet->endpoint = ep;
      onContactSeen(packet->sender, ep);
      #define CASE(type) \
        else if (packet::type* p = dynamic_cast<packet::type*>(packet.get()))
      if (false) {}
      CASE(Ping)
      {
        (void)p;
        packet::Pong r;
        r.sender = _self;
        r.remote_endpoint = ep;
        auto s = elle::serialization::json::serialize(&r);
        send(s, ep);
      }
      CASE(Pong)
        onPong(p);
      CASE(FindValue)
        onFindValue(p);
      CASE(FindNode)
        onFindNode(p);
      CASE(FindNodeReply)
        onFindNodeReply(p);
      CASE(FindValueReply)
        onFindValueReply(p);
      CASE(Store)
        onStore(p);
      else
          ELLE_WARN("%s: Unknown packet type %s", *this, typeid(*p).name());
#undef CASE
    }
  }

  void Kademlia::send(elle::Buffer const& b, Endpoint e)
  {
    reactor::Lock l(_udp_send_mutex);
    ELLE_DUMP("%s: sending packet to %s\n%s", *this, e, b.string());
    _socket.send_to(reactor::network::Buffer(b.contents(), b.size()), e);
  }
  bool Kademlia::more(Address const& a, Address const& b)
  {
    if (a == b)
      return false;
    return !less(a, b);
  }
  bool Kademlia::less(Address const& a, Address const& b)
  {
    const Address::Value &aa = a.value();
    const Address::Value &bb = b.value();
    const Address::Value &mm = _mask.value();
    for (int p=sizeof(Address::Value)-1; p>=0; --p)
    {
      unsigned char va = aa[p] & mm[p];
      unsigned char vb = bb[p] & mm[p];
      if (va < vb)
        return true;
      if (va > vb)
        return false;
    }
    return false;
  }
  Address Kademlia::dist(Address const& a, Address const& b)
  {
    auto va = a.value();
    auto vb = b.value();
    auto mv = _mask.value();
    Address::Value r;
    for (unsigned int i=0; i<sizeof(r); ++i)
      r[i] = (va[i] ^ vb[i]) & mv[i];
    return Address(r);
  }
  int Kademlia::bucket_of(Address const& a)
  {
    Address d = dist(a, _config.node_id);
    auto dv = d.value();
    for (int p=sizeof(Address::Value)-1; p>=0; --p)
    {
      if (dv[p])
      {
        for (int b=7; b>=0; --b)
        {
          if (dv[p] & (1<<b))
            return p*8 + b;
        }
      }
    }
    return 0;
  }


  void Kademlia::_reload(Local& l)
  {
    while (_local_endpoint == Endpoint())
    {
      ELLE_TRACE("%s: Waiting for endpoint (ping)", *this);
      reactor::sleep(500_ms);
    }
    auto keys = l.storage()->list();
    for (auto const& k: keys)
    {
      _storage[k].push_back(Store{_local_endpoint, now()});
      std::shared_ptr<Query> q = startQuery(k, false);
      ELLE_TRACE("%s: waiting for reload query", *this);
      q->barrier.wait();
      ELLE_TRACE("%s: reload query finished", *this);
      packet::Store s;
      s.sender = _self;
      s.key = k;
      s.value = {_local_endpoint};
      auto buf = elle::serialization::json::serialize(&s);
      // store the mapping in the k closest nodes
      for (unsigned int i=0; i<q->res.size() && i<unsigned(_config.k); ++i)
      {
        send(buf, q->endpoints.at(q->res[i]));
      }
    }
  }

  reactor::Generator<Kademlia::Member>
  Kademlia::_lookup(infinit::model::Address address,
                    int n, infinit::overlay::Operation op) const
  {
    ELLE_TRACE("%s: lookup %s with mode %s", *this, address, op);
    auto self = const_cast<Kademlia*>(this);
    if (op == infinit::overlay::OP_INSERT)
    { // Lets try an insert policy of 'closest nodes'
      std::shared_ptr<Query> q =
      self->startQuery(address, false);
      ELLE_TRACE("%s: waiting for insert query", *this);
      q->barrier.wait();
      ELLE_TRACE("%s: insert query finished", *this);
      // pick the closest node found to store
      packet::Store s;
      s.sender = _self;
      s.key = address;
      s.value = {q->endpoints.at(q->res[0])};
      auto buf = elle::serialization::json::serialize(&s);
      // store the mapping in the k closest nodes
      for (unsigned int i=0; i<q->res.size() && i<unsigned(_config.k); ++i)
      {
        self->send(buf, q->endpoints.at(q->res[i]));
      }
      infinit::overlay::Overlay::Members res;
      ELLE_TRACE("%s: Connecting remote %s", *this, s.value[0]);
      boost::asio::ip::tcp::endpoint ep;
      endpoint_to_endpoint(s.value[0], ep);
      res.emplace_back(
        new infinit::model::doughnut::Remote(
          const_cast<infinit::model::doughnut::Doughnut&>(*this->doughnut()),
          /* FIXME BEARCLAW */ Address(),
          ep));
      ELLE_TRACE("%s: returning", *this);
      return reactor::generator<Member>(
        [res] (reactor::yielder<Member>::type const& yield)
        {
          for (auto r: res) yield(r);
        });
    }

    std::shared_ptr<Query> q = self->startQuery(address, true);
    ELLE_TRACE("%s: waiting for value query", *this);
    q->barrier.wait();
    ELLE_TRACE("%s: waiting done", *this);
    if (q->storeResult.empty() && op == infinit::overlay::OP_INSERT_OR_UPDATE)
    {
      return _lookup(address, n, infinit::overlay::OP_INSERT);
    }
    infinit::overlay::Overlay::Members res;
    if (!q->storeResult.empty())
    {
      boost::asio::ip::tcp::endpoint ep;
      endpoint_to_endpoint(q->storeResult[0], ep);
      res.emplace_back(
        new infinit::model::doughnut::Remote(
          const_cast<infinit::model::doughnut::Doughnut&>(*this->doughnut()),
          /* FIXME BEARCLAW */ Address(),
          ep));
    }
    else
      throw infinit::model::MissingBlock(address);
    return reactor::generator<Member>([res]  (reactor::yielder<Member>::type const& yield)
      {
        for (auto r: res) yield(r);
      });
  }

  Kademlia::Member Kademlia::_lookup_node(infinit::model::Address address)
  {
    return Overlay::Member();
  }

  static int qid = 0;
  std::shared_ptr<Kademlia::Query> Kademlia::startQuery(Address const& target, bool storage)
  {
    auto sq = std::make_shared<Query>();
    int id = ++qid;
    sq->target = target;
    sq->pending = 0;
    sq->n = 1;
    sq->steps = 0;
    _queries.insert(std::make_pair(id, sq));
    // initialize k candidates
    auto map = closest(target);
    if (map.empty())
    {
      return {};
    }
    for (auto const& e: map)
    {
      sq->endpoints[e.first] = e.second;
      sq->candidates.push_back(e.first);
    }
    sq->res = sq->candidates;
    for (int i=0; i<_config.alpha; ++i)
    {
      boost::optional<Address> a = recurseRequest(*sq, {{}});
      if (!a)
      {
        ELLE_TRACE("%s: startQuery %s(%s): no target", *this,
          id, storage);
        continue;
      }

      elle::Buffer buf;
      if (storage)
      {
        packet::FindValue fv;
        fv.sender = _self;
        fv.requestId = id;
        fv.target = target;
        buf = elle::serialization::json::serialize(&fv);
      }
      else
      {
        packet::FindNode fn;
        fn.sender = _self;
        fn.requestId = id;
        fn.target = target;
        buf = elle::serialization::json::serialize(&fn);
      }
      ELLE_TRACE("%s: startquery %s(%s) send to %s",
        *this, id, storage, *a);
      send(buf, sq->endpoints.at(*a));
    }

    return sq;
  }

  void Configuration::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("node_id", node_id);
    s.serialize("port", port);
    s.serialize("bootstrap_nodes", bootstrap_nodes);
    s.serialize("wait", wait);
    s.serialize("address_size", address_size);
    s.serialize("k", k);
    s.serialize("alpha", alpha);
    s.serialize("ping_interval_ms", ping_interval_ms);
    s.serialize("refresh_interval_ms", refresh_interval_ms);
    s.serialize("storage_lifetime_ms", storage_lifetime_ms);
    s.serialize("wait_ms", wait_ms);
  }

  void Kademlia::store(infinit::model::blocks::Block const& block,
                       infinit::model::StoreMode mode)
  {
    // advertise it
    ELLE_TRACE("%s: Advertizing %x", *this, block.address());
  }
  void Kademlia::remove(Address address)
  {
  }

  void Kademlia::fetch(Address address, std::unique_ptr<infinit::model::blocks::Block> & b)
  {
  }

  void Kademlia::print(std::ostream& o) const
  {
    o << "Kad(" << _socket.local_endpoint() << ')';
  }

  void Kademlia::onContactSeen(Address sender, Endpoint ep)
  {
    int b = bucket_of(sender);
    ELLE_DUMP("%s: %x -> %s", *this, sender, b);
    ELLE_ASSERT(unsigned(b) < _routes.size());
    auto& bucket = _routes[b];
    auto it = std::find_if(bucket.begin(), bucket.end(),
      [&](Node const& a) { return a.address == sender;});
    if (it != bucket.end())
    {
      it->endpoint = ep;
      it->last_seen = now();
    }
    else if (bucket.size() < unsigned(_config.k))
    {
      ELLE_DEBUG("Inserting new node %x(%s)", sender, ep);
      bucket.push_back(Node{sender, ep, now(), 0});
    }
    else
    { // FIXME store in backup list or maybe kick a bad node
      ELLE_DEBUG("Dropping extra node %x(%s)", sender, ep);
    }
  }

  void Kademlia::onPong(packet::Pong* p)
  {
    _local_endpoint = p->remote_endpoint;
    Address addr = p->sender;
    int b = bucket_of(addr);
    auto& bucket = _routes[b];
    auto it = std::find_if(bucket.begin(), bucket.end(),
      [&](Node const& a) { return a.address == addr;});
    if (it == bucket.end())
      return;
    it->last_seen = now();
    it->endpoint = p->endpoint;
    it->unack_ping = 0;
  }

  std::unordered_map<Address, Endpoint> Kademlia::closest(Address addr)
  {
    std::unordered_map<Address, Endpoint> result;
    int b = bucket_of(addr);
    for (; b>= 0; --b)
    {
      auto& bucket = _routes[b];
      for (auto const& e: bucket)
      {
        result[e.address] = e.endpoint;
        if (result.size() == unsigned(_config.k))
          return result;
      }
    }
    for (b = bucket_of(addr)+1; unsigned(b) < _routes.size(); ++b)
    {
      auto& bucket = _routes[b];
      for (auto const& e: bucket)
      {
        result[e.address] = e.endpoint;
        if (result.size() == unsigned(_config.k))
          return result;
      }
    }
    return result;
  }

  void Kademlia::onFindNode(packet::FindNode* p)
  {
    std::unordered_map<Address, Endpoint> result = closest(p->target);
    packet::FindNodeReply res;
    res.sender = _self;
    res.nodes = result;
    res.requestId = p->requestId;
    send(elle::serialization::json::serialize(&res), p->endpoint);
  }

  void Kademlia::onFindValue(packet::FindValue* p)
  {
    ELLE_DEBUG("%s: onFindValue %s", *this, p->requestId);
    std::unordered_map<Address, Endpoint> result = closest(p->target);
    packet::FindValueReply res;
    res.sender = _self;
    res.nodes = result;
    res.requestId = p->requestId;
    auto it = _storage.find(p->target);
    if (it != _storage.end())
    {
      for (auto const& s: it->second)
        res.results.push_back(s.endpoint);
    }
    ELLE_DEBUG("%s: onFindValue %s replying with %s nodes and %s results",
      *this, p->requestId, res.nodes.size(), res.results.size());
    send(elle::serialization::json::serialize(&res), p->endpoint);
  }

  void Kademlia::onStore(packet::Store* p)
  {
    ELLE_DEBUG("%s: Storing %x at %x", *this, p->key, _self);
    auto& e = _storage[p->key];
    for (auto const& ep: p->value)
    {
      auto it = std::find_if(e.begin(), e.end(),
        [&](Store const& s) { return s.endpoint == ep;});
      if (it != e.end())
        it->last_seen = now();
      else
        e.push_back(Store{ep, now()});
    }
  }

  void Kademlia::finish(int rid, Query& q)
  {
    std::sort(q.res.begin(), q.res.end(),
      [&](Address const& a, Address const& b) -> bool {
        return less(dist(a, q.target), dist(b, q.target));
      });
    q.barrier.open();
    _queries.erase(rid);
  }

  boost::optional<Address> Kademlia::recurseRequest(
    Query& q,
    std::unordered_map<Address, Endpoint> const& nodes)
  {
    ++q.steps;
    for (auto const& r: nodes)
    {
      if (r.first == _self)
        continue;
      onContactSeen(r.first, r.second);
      q.endpoints[r.first] = r.second;
      if (std::find(q.queried.begin(), q.queried.end(),r.first) == q.queried.end()
        && std::find(q.candidates.begin(), q.candidates.end(), r.first) == q.candidates.end())
        q.candidates.push_back(r.first);
      if (std::find(q.res.begin(), q.res.end(), r.first) == q.res.end())
        q.res.push_back(r.first);
    }
    if (q.candidates.empty())
    {
      ELLE_TRACE("%s: no more candidates", *this);
      return {};
    }
    auto addrIt = std::min_element(q.candidates.begin(), q.candidates.end(),
      [&](Address const& a, Address const& b) -> bool {
        return less(dist(a, q.target), dist(b, q.target));
      });
    Address addr = *addrIt;
    // stop query if we already queried the k closest nodes we know about
    std::sort(q.res.begin(), q.res.end(),
      [&](Address const& a, Address const& b) -> bool {
        return less(dist(a, q.target), dist(b, q.target));
      });
    if (q.steps >= 3 && q.res.size() >= unsigned(_config.k)
      && less(dist(q.res[_config.k-1], q.target),
              dist(addr, q.target)))
    {
      ELLE_TRACE("%s: fetched enough", *this);
      return {};
    }
    std::swap(q.candidates[addrIt - q.candidates.begin()], q.candidates.back());
    q.candidates.pop_back();
    q.queried.push_back(addr);
    ++q.pending;
    return addr;
  }

  void Kademlia::onFindNodeReply(packet::FindNodeReply* p)
  {
    auto it = _queries.find(p->requestId);
    if (it == _queries.end())
    {
      ELLE_DEBUG("%s: query %s is gone", *this, p->requestId);
      return;
    }
    ELLE_DEBUG("%s: query %s got %s nodes", *this, p->requestId, p->nodes.size());
    auto& q = *it->second;
    --q.pending;
    boost::optional<Address> addr = recurseRequest(q, p->nodes);
    if (!addr)
    {
      finish(it->first, q);
      return;
    }
    ELLE_DEBUG("%s: query %s passed to %x", *this, p->requestId, *addr);
    packet::FindNode fn;
    fn.requestId = it->first;
    fn.sender = _self;
    fn.target = q.target;
    send(elle::serialization::json::serialize(&fn), q.endpoints.at(*addr));
  }
  void Kademlia::onFindValueReply(packet::FindValueReply * p)
  {
    ELLE_DEBUG("%s: findvalue reply %s", *this, p->requestId);
    auto it = _queries.find(p->requestId);
    if (it == _queries.end())
    {
      ELLE_DEBUG("%s: query %s is gone", *this, p->requestId);
      return;
    }
    auto& q = *it->second;
    --q.pending;
    for (auto ep: p->results)
      q.storeResult.push_back(ep);
    if (q.storeResult.size() >= unsigned(q.n))
    {
      ELLE_DEBUG("%s: got enough results on %s", *this, p->requestId);
      finish(it->first, q);
      return;
    }
    boost::optional<Address> addr = recurseRequest(q, p->nodes);
    if (!addr)
    {
      ELLE_DEBUG("%s: no more peers on %s", *this, p->requestId);
      finish(it->first, q);
      return;
    }
    packet::FindValue fv;
    fv.sender = _self;
    fv.target = q.target;
    fv.requestId = it->first;
    auto buf = elle::serialization::json::serialize(&fv);
    ELLE_DEBUG("%s: forwarding value query %s to %x", *this, p->requestId, *addr);
    send(buf, q.endpoints.at(*addr));
  }

  void Kademlia::_republish()
  {
    reactor::sleep(boost::posix_time::seconds(rand() % 10));
    while (true)
    {
      // count how many keys we have
      int count = 0;
      for (auto & sv: _storage)
        for (auto& s: sv.second)
          if (s.endpoint == _local_endpoint)
            ++count;
      if (!count)
      {
        reactor::sleep(10_sec);
        continue;
      }
      int interval = _config.storage_lifetime_ms / count;
      ELLE_DEBUG("%s: set refresh interval to %s", *this, interval);
      reactor::sleep(boost::posix_time::milliseconds(interval/3));
      Address oldest;
      Time t = now();
      Store* match = nullptr;
      for (auto & sv: _storage)
        for (auto& s: sv.second)
          if (s.endpoint == _local_endpoint && s.last_seen < t)
          {
            match = &s;
            t = s.last_seen;
            oldest = sv.first;
          }
      match->last_seen = now();
      std::shared_ptr<Query> q = startQuery(oldest, false);
      ELLE_TRACE("%s: waiting for republish query", *this);
      q->barrier.wait();
      ELLE_TRACE("%s: republish query finished", *this);
      packet::Store s;
      s.sender = _self;
      s.key = oldest;
      s.value = {_local_endpoint};
      auto buf = elle::serialization::json::serialize(&s);
      for (unsigned int i=0; i<q->res.size() && i<unsigned(_config.k); ++i)
      {
        send(buf, q->endpoints.at(q->res[i]));
      }
    }
  }

  void Kademlia::_cleanup()
  {
    while (true)
    {
      reactor::sleep(10_sec);
      for (auto& s: _storage)
      {
        for (unsigned int i=0; i < s.second.size(); ++i)
        {
          if (now() - s.second[i].last_seen > std::chrono::milliseconds(_config.storage_lifetime_ms))
          {
            ELLE_DEBUG("%s: cleanup %x -> %s", *this, s.first, s.second[i].endpoint);
            std::swap(s.second[i], s.second[s.second.size()-1]);
            s.second.pop_back();
            --i;
          }
        }
      }
    }
  }

  void Kademlia::_ping()
  {
    reactor::sleep(boost::posix_time::milliseconds(rand() % _config.ping_interval_ms));
    while (true)
    {
      reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
      int ncount = 0;
      for (auto const& b: _routes)
        ncount += b.size();
      ELLE_DEBUG("%s: knows %s nodes", *this, ncount);
      if (ncount == 0)
        continue;
      unsigned int target = rand() % ncount;
      unsigned int p = 0;
      for (auto& b: _routes)
      {
        if (p + b.size() > target)
        {
          auto& node = b[target - p];
          node.unack_ping++;
          packet::Ping pi;
          pi.sender = _self;
          pi.remote_endpoint = node.endpoint;
          send(elle::serialization::json::serialize(&pi), node.endpoint);
          break;
        }
        p += b.size();
      }
    }
  }
  void Kademlia::_refresh()
  {
    reactor::sleep(boost::posix_time::milliseconds(rand() % _config.refresh_interval_ms));
    while (true)
    {
      ELLE_DEBUG("%s: refresh query", *this);
      auto sq = startQuery(_self, false);
      if (sq)
      {
        sq->barrier.wait();
        ELLE_DEBUG("%s: refresh query finished", *this);
      }
      reactor::sleep(boost::posix_time::milliseconds(_config.refresh_interval_ms));
    }
  }
}

namespace infinit
{
  namespace overlay
  {
    namespace kademlia
    {
      Configuration::Configuration()
      {}

      Configuration::Configuration(elle::serialization::SerializerIn& input)
      {
        this->serialize(input);
      }

      void
      Configuration::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("config", this->config);
      }

      std::unique_ptr<infinit::overlay::Overlay>
      Configuration::make(
        model::Address id,
        NodeEndpoints const& hosts,
        std::shared_ptr<infinit::model::doughnut::Local> local,
        model::doughnut::Doughnut* doughnut)
      {
        for (auto const& host: hosts)
          for (auto const& ep: host.second)
        config.bootstrap_nodes.push_back(
          elle::serialization::Serialize< ::kademlia::PrettyEndpoint>
          ::convert(ep));
        return elle::make_unique< ::kademlia::Kademlia>(
          id, config, std::move(local), doughnut);
      }
      static const elle::serialization::Hierarchy<overlay::Configuration>::
      Register<Configuration> _registerKademliaOverlayConfig("kademlia");
    }
  }
}
