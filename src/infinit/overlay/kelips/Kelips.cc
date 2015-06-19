#include <infinit/overlay/kelips/Kelips.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <reactor/network/buffer.hh>
#include <reactor/exception.hh>
#include <reactor/scheduler.hh>
#include <reactor/thread.hh>
#include <reactor/Barrier.hh>
#include <reactor/Scope.hh>

#include <random>
#include <algorithm>

ELLE_LOG_COMPONENT("infinit.overlay.kelips");

namespace elle
{
  namespace serialization
  {
    template<typename T>
    struct SerializeEndpoint
    {
      typedef std::string Type;
      static std::string convert(T& ep)
      {
        return ep.address().to_string() + ":" + std::to_string(ep.port());
      }
      static T convert(std::string& repr)
      {
        size_t sep = repr.find_first_of(':');
        auto addr = boost::asio::ip::address::from_string(repr.substr(0, sep));
        int port = std::stoi(repr.substr(sep + 1));
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
        return std::chrono::duration_cast<std::chrono::milliseconds>(
          t.time_since_epoch()).count();
      }
      static kelips::Time convert(uint64_t repr)
      {
        return kelips::Time(std::chrono::milliseconds(repr));
      }
    };
  }
}
namespace kelips
{
  namespace packet
  {
    #define REGISTER(classname, type) \
    static const elle::serialization::Hierarchy<Packet>:: \
    Register<classname>   \
    _registerPacket##classname(type)

    struct Packet
    : public elle::serialization::VirtuallySerializable
    {
      GossipEndpoint endpoint;
      Address sender;
      static constexpr char const* virtually_serializable_key = "type";
    };
    
    struct Ping: public Packet
    {
      Ping() {}
      Ping(elle::serialization::SerializerIn& input) {serialize(input);}
      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("sender", sender);
        s.serialize("endpoint", remote_endpoint);
      }
      GossipEndpoint remote_endpoint;
    };
    REGISTER(Ping, "ping");
    
    struct Pong: public Ping
    {
      using Ping::Ping;
    };
    REGISTER(Pong, "pong");

    struct BootstrapRequest: public Packet
    {
      BootstrapRequest() {}
      BootstrapRequest(elle::serialization::SerializerIn& input) {serialize(input);}
      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("sender", sender);
      }
    };
    REGISTER(BootstrapRequest, "bootstrapRequest");

    struct Gossip: public Packet
    {
      Gossip() {}
      Gossip(elle::serialization::SerializerIn& input) {serialize(input);}
      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("sender", sender);
        s.serialize("contacts", contacts);
        s.serialize("files", files);
      }
      // address -> (last_seen, val)
      std::unordered_map<Address, std::pair<Time, GossipEndpoint>> contacts;
      std::unordered_map<Address, std::pair<Time, Address>> files;
    };
    REGISTER(Gossip, "gossip");

    struct GetFileRequest: public Packet
    {
      GetFileRequest() {}
      GetFileRequest(elle::serialization::SerializerIn& input) {serialize(input);}
      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("sender", sender);
        s.serialize("id", request_id);
        s.serialize("origin", originAddress);
        s.serialize("endpoint", originEndpoint);
        s.serialize("address", fileAddress);
        s.serialize("ttl", ttl);
      }
      int request_id;
      Address originAddress; //origin node
      GossipEndpoint originEndpoint;
      Address fileAddress; //file address requested
      int ttl;
    };
    REGISTER(GetFileRequest, "get");

    struct GetFileReply: public Packet
    {
      GetFileReply() {}
      GetFileReply(elle::serialization::SerializerIn& input) {serialize(input);}
      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("sender", sender);
        s.serialize("id", request_id);
        s.serialize("origin", origin);
        s.serialize("endpoint", resultEndpoint);
        s.serialize("address", fileAddress);
        s.serialize("result", resultAddress);
        s.serialize("ttl", ttl);
      }
      int request_id;
      Address origin; // node who created the request
      Address resultAddress; // home node of the file or 0 for failure
      RpcEndpoint resultEndpoint; // endpoint of home node, 0 port for failure
      Address fileAddress;
      int ttl;
    };
    REGISTER(GetFileReply, "getReply");

    struct PutFileRequest: public GetFileRequest
    {
      using GetFileRequest::GetFileRequest;
    };
    REGISTER(PutFileRequest, "put");

    struct PutFileReply: public GetFileReply
    {
      using GetFileReply::GetFileReply;
    };
    REGISTER(PutFileReply, "putReply");

#undef REGISTER

    template<typename T> elle::Buffer serialize(T const& packet)
    {
      elle::Buffer buf;
      elle::IOStream stream(buf.ostreambuf());
      elle::serialization::json::SerializerOut output(stream, false);
      output.serialize_forward((packet::Packet const&)packet);
      //const_cast<T&>(packet).serialize(output);
      return buf;
    }
  }

  struct PendingRequest
  {
    RpcEndpoint result;
    reactor::Barrier barrier;
  };

  static inline Time now()
  {
    return std::chrono::steady_clock::now();
  }

  template<typename C>
  typename C::iterator random_from(C& container, std::default_random_engine& gen)
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
  C pick_n(C const& src, int count, G& generator)
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
  C remove_n(C const& src, int count, G& generator)
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

  template<typename E1, typename E2>
  void endpoint_to_endpoint(E1 const& src, E2& dst)
  {
    dst = E2(src.address(), src.port());
  }

  Node::Node(Configuration const& config,
             std::unique_ptr<infinit::storage::Storage> storage)
  : Local(std::move(storage), config.port)
  , _config(config)
  {
    start();
  }

  int Node::group_of(Address const& a)
  {
    auto address = a.value();
    unsigned int addr4 = address[0]
      + (address[1] << 8)
      + (address[2] << 16)
      + (address[3] << 24);
    return addr4 % _config.k;
  }

  void Node::start()
  {
    if (_config.node_id == Address())
    {
      _config.node_id = Address::random();
      std::cout << "Generating node_id:" << std::endl;
      elle::serialization::json::SerializerOut output(std::cout);
      output.serialize_forward(_config.node_id);
    }
    _self = _config.node_id;
    _group = group_of(_config.node_id);
    _state.contacts.resize(_config.k);
    if (!_config.port)
    { // Use same port as local
      _config.port = Local::server_endpoint().port();
    }
    _gossip.socket()->close();
    _gossip.bind(GossipEndpoint({}, _config.port));
    ELLE_TRACE("%s: bound to udp, member of group %s", *this, _group);
    _emitter_thread = elle::make_unique<reactor::Thread>("emitter",
      std::bind(&Node::gossipEmitter, this));
    _listener_thread = elle::make_unique<reactor::Thread>("listener",
      std::bind(&Node::gossipListener, this));
    _pinger_thread = elle::make_unique<reactor::Thread>("pinger",
      std::bind(&Node::pinger, this));
    // Send a bootstrap request to bootstrap nodes
    packet::BootstrapRequest req;
    req.sender = _config.node_id;
    elle::Buffer buf = packet::serialize(req);
    for (auto const& e: _config.bootstrap_nodes)
    {
      send(buf, e);
    }
    if (_config.wait)
      wait(_config.wait);
  }

  void Node::send(elle::Buffer const& b, GossipEndpoint e)
  {
    reactor::Lock l(_udp_send_mutex);
    ELLE_DUMP("%s: sending packet to %s\n%s", *this, e, b.string());
    _gossip.send_to(reactor::network::Buffer(b.contents(), b.size()), e);
  }
 

  void Node::gossipListener()
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
      ELLE_DUMP("%s: received data from %s:\n%s", *this, source, buf.string());
      //deserialize
      std::unique_ptr<packet::Packet> packet;
      elle::IOStream stream(buf.istreambuf());
      elle::serialization::json::SerializerIn input(stream);
      try
      {
        input.serialize_forward(packet);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("%s: Failed to deserialize packet: %s", *this, e);
        continue;
      }
      packet->endpoint = source;
      onContactSeen(packet->sender, source);
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
        elle::Buffer s = packet::serialize(r);
        send(s, source);
      }
      CASE(Gossip)
        onGossip(p);
      CASE(BootstrapRequest)
        onBootstrapRequest(p);
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
    }
  }

  template<typename T, typename U, typename G>
  void filterAndInsert(std::vector<Address> files, int target_count,
                       std::unordered_map<Address, std::pair<Time, T>>& res,
                       std::unordered_map<Address, U>& data,
                       T U::*access,
                       G& gen
                       )
  {
    if (files.size() > target_count)
    {
      if (target_count < files.size() - target_count)
        files = pick_n(files, target_count, gen);
      else
        files = remove_n(files, files.size() - target_count, gen);
    }
    for (auto const& f: files)
    {
      auto& fd = data.at(f);
      res.insert(std::make_pair(f,
        std::make_pair(fd.last_seen, fd.*access)));
      fd.last_gossip = now();
      fd.gossip_count++;
    }
  }
  
  void Node::filterAndInsert(std::vector<Address> files, int target_count, int group,
                             std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& res)
  {
    ::kelips::filterAndInsert(files, target_count, res, _state.contacts[group],
                              &Contact::endpoint, _gen);
  }
  void Node::filterAndInsert(std::vector<Address> files, int target_count,
                             std::unordered_map<Address, std::pair<Time, Address>>& res)
  {
    ::kelips::filterAndInsert(files, target_count, res, _state.files,
                              &File::home_node, _gen);
  }

  void filterAndInsert2(std::vector<Contact*> new_contacts, unsigned int max_new,
    std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& res,
    std::default_random_engine gen
    )
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
      res.insert(std::make_pair(f->address,
        std::make_pair(f->last_seen, f->endpoint)));
      f->last_gossip = now();
      f->gossip_count++;
    }
  }

  std::unordered_map<Address, std::pair<Time, GossipEndpoint>>
  Node::pickContacts()
  {
    std::unordered_map<Address, std::pair<Time, GossipEndpoint>> res;
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
      if (f.second.last_gossip < f.second.last_seen
        && now() - f.second.last_seen > std::chrono::milliseconds(_config.gossip.old_threshold_ms)
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
        if (f.second.last_gossip < f.second.last_seen
        && now() - f.second.last_seen > std::chrono::milliseconds(_config.gossip.old_threshold_ms)
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
  
  std::unordered_map<Address, std::pair<Time, Address>>
  Node::pickFiles()
  {
    std::unordered_map<Address, std::pair<Time, Address>> res;
    unsigned int max_new = _config.gossip.files / 2;
    unsigned int max_old = _config.gossip.files / 2;
    // insert new files
    std::vector<Address> new_files;
    for (auto const& f: _state.files)
    {
      if (f.second.gossip_count < _config.gossip.new_threshold)
        new_files.push_back(f.first);
    }
    filterAndInsert(new_files, max_new, res);
    // insert old files, only our own for which we can update the last_seen value
    std::vector<Address> old_files;
    for (auto& f: _state.files)
    {
      // update self file last seen, this will avoid us some ifs at other places
      if (f.second.home_node == _self)
        f.second.last_seen = now();
      if (f.second.home_node == _self
        && ((now() - f.second.last_gossip) > std::chrono::milliseconds(_config.gossip.old_threshold_ms))
        && res.find(f.first) == res.end()
        )
        old_files.push_back(f.first);
    }
    filterAndInsert(old_files, max_old, res);
    // Check if we have room for more files
    if (res.size() < (unsigned)_config.gossip.files)
    {
      int n = _config.gossip.files - res.size();
      // Pick at random
      std::vector<Address> available;
      for (auto const& f: _state.files)
      {
        if (res.find(f.first) == res.end())
          available.push_back(f.first);
      }
      filterAndInsert(available, n, res);
    }
    return res;
  }

  void Node::gossipEmitter()
  {
    std::uniform_int_distribution<> random(0, _config.gossip.interval_ms);
    int v = random(_gen);
    reactor::sleep(boost::posix_time::milliseconds(v));
    packet::Gossip p;
    p.sender = _self;
    while (true)
    {
      reactor::sleep(boost::posix_time::millisec(_config.gossip.interval_ms));
      p.contacts.clear();
      p.files.clear();

      p.contacts = pickContacts();
      elle::Buffer buf = serialize(p);
      std::vector<GossipEndpoint> targets = pickOutsideTargets();
      for (auto const& e: targets)
        send(buf, e);
      // Add some files, just for group targets
      p.files = pickFiles();
      buf = serialize(p);
      targets = pickGroupTargets();
      for (auto const& e: targets)
        send(buf, e);
    }
  }

  void Node::onContactSeen(Address addr, GossipEndpoint endpoint)
  {
    ELLE_TRACE("%s: onContactSeen %x: %s", *this, addr, endpoint);
    int g = group_of(addr);
    Contacts& target = _state.contacts[g];

    auto it = target.find(addr);
    // FIXME: limit size of other contacts
    if (it == target.end())
    {
      if (g == _group || target.size() < (unsigned)_config.max_other_contacts)
        target[addr] = Contact{endpoint, addr, Duration(0), now(), Time(), 0};
    }
    else
    {
      it->second.last_seen = std::chrono::steady_clock::now();
      it->second.endpoint = endpoint;
    }
  }

  void Node::onPong(packet::Pong* p)
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
      it->second.last_seen = now();
      it->second.rtt = d;
    }
    _ping_target = Address();
    _ping_barrier.open();
    GossipEndpoint endpoint = p->remote_endpoint;
    if (_local_endpoint.port() == 0)
    {
      ELLE_TRACE("%s: Setting local endpoint to %s", *this, endpoint);
      _local_endpoint = endpoint;
    }
    else if (_local_endpoint != endpoint)
      ELLE_WARN("%s: Received a different local endpoint: %s, current %s", *this,
        endpoint, _local_endpoint);
  }

  void Node::onGossip(packet::Gossip* p)
  {
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
          target[c.first] = Contact{c.second.second, c.first, Duration(), c.second.first, Time(), 0};
      }
      else if (c.second.first > it->second.last_seen)
      { // Also update endpoint in case it changed
        it->second.last_seen = c.second.first;
        it->second.endpoint = c.second.second;
      }
    }
    if (g == _group)
    {
      for (auto const& f: p->files)
      {
        auto it = _state.files.find(f.first);
        if (it == _state.files.end())
          _state.files[f.first] = File{f.first, f.second.second, f.second.first, Time(), 0};
        else
        {
          it->second.last_seen = std::max(it->second.last_seen, f.second.first);
        }
      }
    }
  }

  void Node::onBootstrapRequest(packet::BootstrapRequest* p)
  {
    int g = group_of(p->sender);
    packet::Gossip res;
    res.sender = _self;
    int group_count = _state.contacts[g].size();
    // Special case to avoid the randomized fetcher running for ever
    if (group_count <= _config.gossip.bootstrap_group_target + 5)
    {
      for (auto const& e: _state.contacts[g])
        res.contacts.insert(std::make_pair(e.first,
          std::make_pair(e.second.last_seen, e.second.endpoint)));
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
          res.contacts[it->first] = std::make_pair(it->second.last_seen, it->second.endpoint);
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
            res.contacts.insert(std::make_pair(e.first,
              std::make_pair(e.second.last_seen, e.second.endpoint)));
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
          res.contacts[it->first] = std::make_pair(it->second.last_seen, it->second.endpoint);
      }
    }
    elle::Buffer buf = serialize(res);
    send(buf, p->endpoint);
  }

  void Node::onGetFileRequest(packet::GetFileRequest* p)
  {
    int fg = group_of(p->fileAddress);
    if (fg == _group)
    {
      auto file_it = _state.files.find(p->fileAddress);
      if (file_it != _state.files.end())
      {
        RpcEndpoint endpoint;
        bool found = false;
        if (file_it->second.home_node == _self)
        {
          endpoint_to_endpoint(_local_endpoint, endpoint);
          found = true;
        }
        else
        {
          auto contact_it = _state.contacts[fg].find(file_it->second.home_node);
          if (contact_it != _state.contacts[fg].end())
          {
            endpoint_to_endpoint(contact_it->second.endpoint, endpoint);
            found = true;
          }
        }
        if (found)
        {
          packet::GetFileReply res;
          res.sender = _self;
          res.fileAddress = p->fileAddress;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          endpoint_to_endpoint(endpoint, res.resultEndpoint);
          res.ttl = 1;
          elle::Buffer buf = serialize(res);
          send(buf, p->originEndpoint);
          // FIXME: should we route the reply back the same path?
          return;
        }
      }
    }
    // We don't have it, route the request,
    if (p->ttl == 0)
    {
      // FIXME not in initial protocol, but we cant distinguish get and put
      packet::GetFileReply res;
      res.resultEndpoint = RpcEndpoint();
      res.sender = _self;
      res.fileAddress = Address();
      res.origin = p->originAddress;
      res.request_id = p->request_id;
      res.ttl = 1;
      elle::Buffer buf = serialize(res);
      send(buf, p->originEndpoint);
      return;
    }
    p->ttl--;
    p->sender = _self;
    int count = _state.contacts[fg].size();
    std::uniform_int_distribution<> random(0, count-1);
    int idx = random(_gen);
    auto it = _state.contacts[fg].begin();
    while (idx--) ++it;
    elle::Buffer buf = serialize(*p);
    send(buf, it->second.endpoint);
  }

  void Node::onGetFileReply(packet::GetFileReply* p)
  {
    auto it = _pending_requests.find(p->request_id);
    if (it == _pending_requests.end())
    {
      ELLE_TRACE("%s: Unknown request id %s", *this, p->request_id);
      return;
    }
    ELLE_DEBUG("%s: unlocking waiter on response %s: %s", *this, p->request_id,
               p->resultEndpoint);
    it->second->result = p->resultEndpoint;
    it->second->barrier.open();
    _pending_requests.erase(it);
  }

  void Node::onPutFileRequest(packet::PutFileRequest* p)
  {
    if (p->ttl > 0)
    { // Forward the packet to an other node
      int fg = group_of(p->fileAddress);
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
      elle::Buffer buf = serialize(*p);
      send(buf, it->second.endpoint);
    }
    else
    { // That makes us the home node for this address, but
      // wait until we get the RPC to store anything
      packet::PutFileReply res;
      res.sender = _self;
      res.request_id = p->request_id;
      res.origin = p->originAddress;
      endpoint_to_endpoint(_local_endpoint, res.resultEndpoint);
      res.fileAddress = p->fileAddress;
      res.resultAddress = _self;
      res.ttl = 1;
      elle::Buffer buf = serialize(res);
      send(buf, p->originEndpoint);
    }
  }

  void Node::onPutFileReply(packet::PutFileReply* p)
  {
    onGetFileReply(p);
  }

  RpcEndpoint Node::address(Address file, reactor::DurationOpt timeout)
  {
    try
    {
      return lookup<packet::GetFileRequest>(file, timeout);
    }
    catch (reactor::Timeout const& e)
    {
      ELLE_TRACE("%s: get failed on %x, trying fetch", *this, file);
      return lookup<packet::PutFileRequest>(file, timeout);
    }
  }
  template<typename T>
  RpcEndpoint Node::lookup(Address file, reactor::DurationOpt timeout)
  {
    int fg = group_of(file);
    boost::optional<RpcEndpoint> res;
    T p;
    p.sender = _self;
    p.originAddress = _self;
    endpoint_to_endpoint(_local_endpoint, p.originEndpoint);
    p.fileAddress = file;
    p.ttl = 3 * ceil(log(pow(_config.k, 2)));
    elle::With<reactor::Scope>() << [&](reactor::Scope& s)
    {
      s.run_background("get address", [&] {
          for (int i=0; i < _config.query_retries; ++i)
          {
            auto r = std::make_shared<PendingRequest>();
            r->barrier.close();
            int id = ++_next_id;
            _pending_requests[id] = r;
            p.request_id = id;
            elle::Buffer buf = serialize(p);
            // Select target node
            auto it = random_from(_state.contacts[fg], _gen);
            if (it == _state.contacts[fg].end())
              it = random_from(_state.contacts[_group], _gen);
            if (it == _state.contacts[_group].end())
              throw std::runtime_error("No contacts in self/target groups");
            ELLE_DEBUG("%s: get request %s", *this, i);
            send(buf, it->second.endpoint);
            try
            {
              reactor::wait(r->barrier,
                            boost::posix_time::milliseconds(_config.query_timeout_ms));
            }
            catch (reactor::Timeout const& t)
            {
              ELLE_TRACE("%s: Timeout on attempt %s", *this, i);
            }
            if (r->barrier.opened())
            {
              ELLE_DEBUG("%s: result for request %s: %s", *this, i, r->result);
              if (r->result.port() != 0)
              {
                res = r->result;
                return;
              }
              // else this is a failure, next attempt...
            }
            else
            {
              ELLE_DEBUG("%s: mrou closed barrier on attempt %s", *this, i);
            }
          }
      });
      s.wait(timeout);
      s.terminate_now();
    };
    if (res)
      return *res;
    ELLE_WARN("%s: failed request", *this);
    throw reactor::Timeout(timeout ?
      *timeout
      : boost::posix_time::milliseconds(_config.query_timeout_ms * _config.query_retries));
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
  std::vector<GossipEndpoint> Node::pickOutsideTargets()
  {
    std::vector<GossipEndpoint> res;
    while (res.size() < (unsigned)_config.gossip.other_target)
    {
      unsigned int count = 0;
      double proba_sum = 0;
      for (int i=0; i<_config.k; ++i)
      {
        if (i == _group)
          continue;
        for (auto const& c: _state.contacts[i])
        {
          if (c.second.rtt != Duration())
          {
            double v = c.second.rtt.count() / 1000.0;
            proba_sum += 1.0 / pow(v, 2);
            ++count;
          }
        }
      }
      if (count <= res.size())
        return res;
      std::uniform_real_distribution<double> distribution(0.0, proba_sum);
      double val = distribution(_gen);
      proba_sum = 0.0;
      for (int i=0; i<_config.k; ++i)
      {
        if (i == _group)
          continue;
        for (auto const& c: _state.contacts[i])
        {
          if (c.second.rtt != Duration())
          {
            double v = c.second.rtt.count() / 1000.0;
            proba_sum += 1.0 / pow(v, 2);
            if (proba_sum > val)
            {
              if (std::find(res.begin(), res.end(), c.second.endpoint) == res.end())
              {
                res.push_back(c.second.endpoint);
                break;
              }
            }
          }
        }
      }
    }
    return res;
  }

  std::vector<GossipEndpoint> Node::pickGroupTargets()
  {
    std::vector<GossipEndpoint> res;
    while (res.size() < (unsigned)_config.gossip.group_target)
    {
      unsigned int count = 0;
      double proba_sum = 0;
      for (auto const& c: _state.contacts[_group])
      {
        if (c.second.rtt != Duration())
        {
          double v = c.second.rtt.count() / 1000.0;
          proba_sum += 1.0 / pow(v, 2);
          ++count;
        }
      }
      if (count <= res.size())
        return res;
      std::uniform_real_distribution<double> distribution(0.0, proba_sum);
      double val = distribution(_gen);
      proba_sum = 0.0;
      for (auto const& c: _state.contacts[_group])
      {
        if (c.second.rtt != Duration())
        {
          double v = c.second.rtt.count() / 1000.0;
          proba_sum += 1.0 / pow(v, 2);
          if (proba_sum > val)
          {
            if (std::find(res.begin(), res.end(), c.second.endpoint) == res.end())
            {
              res.push_back(c.second.endpoint);
              break;
            }
          }
        }
      }
    }
    return res;
  }

  void Node::pinger()
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
      std::stringstream ss;
      ss << "f: " << _state.files.size() << "  c:";
      for(auto const& c: _state.contacts)
        ss << c.size() << ' ';
      ELLE_LOG("%s: %s", *this, ss.str());
      // pick a target
      GossipEndpoint endpoint;
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
        endpoint = it->second.endpoint;
        _ping_target = it->first;
        break;
      }
      packet::Ping p;
      p.sender = _self;
      p.remote_endpoint = endpoint;
      elle::Buffer buf = serialize(p);
      _ping_time = now();
      ELLE_DEBUG("%s: pinging %x at %s", *this, _ping_target, endpoint);
      send(buf, endpoint);
      try
      {
        reactor::wait(_ping_barrier,
                      boost::posix_time::milliseconds(_config.ping_timeout_ms));
        ELLE_DUMP("%s: got pong reply", *this);
      }
      catch(reactor::Timeout const& e)
      {
        ELLE_TRACE("%s: Ping timeout on %x", *this, _ping_target);
      }
      // onPong did the job for us
      _ping_barrier.close();
    }
  }

  void Node::cleanup()
  {
    auto it = _state.files.begin();
    auto t = now();
    while (it != _state.files.end())
    {
      if (t - it->second.last_seen > std::chrono::milliseconds(_config.file_timeout_ms))
      {
        ELLE_TRACE("%s: Erasing file %x", *this, it->first);
        it = _state.files.erase(it);
      }
      else
        ++it;
    }
    for (auto& contacts: _state.contacts)
    {
      auto it = contacts.begin();
      while (it != contacts.end())
      {
        if (t - it->second.last_seen > std::chrono::milliseconds(_config.file_timeout_ms))
        {
          ELLE_TRACE("%s: Erasing contact %x", *this, it->first);
          it = contacts.erase(it);
        }
        else
          ++it;
      }
    }
  }

  std::unique_ptr<infinit::model::blocks::Block> Node::fetch(Address address) const
  {
    try
    {
      return Local::fetch(address);
    }
    catch(std::exception const& e)
    {
      ELLE_WARN("%s: Exception fetching %x: %s", *this, address, e.what());
      throw;
    }
  }
  void Node::store(infinit::model::blocks::Block const& block)
  {
    Local::store(block);
    auto it = _state.files.find(block.address());
    if (it == _state.files.end())
      _state.files[block.address()] = File{block.address(), _self, now(), Time(), 0};
  }
  void Node::remove(Address address)
  {
    Local::remove(address);
    auto it = _state.files.find(address);
    if (it != _state.files.end())
      _state.files.erase(it);
  }

  auto Node::_lookup(infinit::model::Address address, int n) const -> Members
  {
    Members res;
    res.push_back(const_cast<Node*>(this)->address(address));
    return res;
  }

  void Node::print(std::ostream& stream) const
  {
    stream << "Kelips(" << _local_endpoint << ')';
  }

  void Node::wait(int count)
  {
    ELLE_TRACE("%s: waiting for %s nodes", *this, count);
    while (true)
    {
      int sum = 0;
      for (auto const& contacts: _state.contacts)
        sum += contacts.size();
      if (sum >= count)
        return;
      ELLE_TRACE("%s: waiting for %s nodes, got %s", *this, count, sum);
      reactor::sleep(1_sec);
    }
  }

  void Node::reload_state()
  {
    auto s = Local::storage().get();
    auto fs = dynamic_cast<infinit::storage::Filesystem*>(s);
    if (!fs)
    {
      ELLE_ERR("Unknown storage implementation, cannot restore state!");
      return;
    }
    boost::filesystem::path root = fs->root();
    auto it = boost::filesystem::directory_iterator(root);
    for (;it !=  boost::filesystem::directory_iterator(); ++it)
    {
      std::string s = it->path().filename().string();
      if (s.substr(0, 2) =! "0x" || s.length()!=66)
        continue;
      Address addr = Address::from_string(s.substr(2));
      _state.files[addr] = File{addr, _self, now(), Time(), 0};
      ELLE_DEBUG("%s: reloaded %s", *this, addr);
    }
  }

  void Configuration::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("node_id", node_id);
    s.serialize("port", port);
    s.serialize("k", k);
    s.serialize("max_other_contacts", max_other_contacts);
    s.serialize("query_retries", query_retries);
    s.serialize("query_timeout_ms", query_timeout_ms);
    s.serialize("contact_timeout_ms", contact_timeout_ms);
    s.serialize("file_timeout_ms", file_timeout_ms);
    s.serialize("ping_interval_ms", ping_interval_ms);
    s.serialize("ping_timeout_ms", ping_timeout_ms);
    s.serialize("gossip", gossip);
    s.serialize("bootstrap_nodes", bootstrap_nodes);
    s.serialize("wait", wait);
  }
}