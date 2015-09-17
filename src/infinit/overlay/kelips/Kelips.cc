#include <infinit/overlay/kelips/Kelips.hh>

#include <algorithm>
#include <random>

#include <boost/filesystem.hpp>

#include <elle/bench.hh>
#include <elle/format/base64.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>
#include <elle/serialization/json.hh>

#include <cryptography/SecretKey.hh>
#include <cryptography/Error.hh>
#include <cryptography/hash.hh>

#include <reactor/Barrier.hh>
#include <reactor/Scope.hh>
#include <reactor/exception.hh>
#include <reactor/network/buffer.hh>
#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <infinit/storage/Filesystem.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/Passport.hh>

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

  ELLE_ATTRIBUTE(std::string, repr);
};

namespace infinit
{
  namespace overlay
  {
    namespace kelips
    {
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

      typedef std::pair<Address, RpcEndpoint> GetFileResult;
      namespace packet
      {
        template<typename T>
        elle::Buffer
        serialize(T const& packet)
        {
          elle::Buffer buf;
          elle::IOStream stream(buf.ostreambuf());
          Serializer::SerializerOut output(stream, false);
          output.serialize_forward((packet::Packet const&)packet);
          //const_cast<T&>(packet).serialize(output);
          return buf;
        }

#define REGISTER(classname, type) \
        static const elle::serialization::Hierarchy<Packet>::   \
        Register<classname>                                     \
        _registerPacket##classname(type)

        struct Packet
          : public elle::serialization::VirtuallySerializable
        {
          GossipEndpoint endpoint;
          Address sender;
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
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            passport.serialize(s);
            s.serialize("sender", sender);
          }

          infinit::model::doughnut::Passport passport;
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
            input.serialize("encrypted_key", encrypted_key);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            passport.serialize(s);
            s.serialize("sender", sender);
            s.serialize("encrypted_key", encrypted_key);
          }

          elle::Buffer encrypted_key;
          infinit::model::doughnut::Passport passport;
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
          }
        };
        REGISTER(BootstrapRequest, "bootstrapRequest");

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
            s.serialize("contacts", contacts);
            s.serialize("files", files);
          }
          // address -> (last_seen, val)
          std::unordered_map<Address, std::pair<Time, GossipEndpoint>> contacts;
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
            s.serialize("id", request_id);
            s.serialize("origin", originAddress);
            s.serialize("endpoint", originEndpoint);
            s.serialize("address", fileAddress);
            s.serialize("ttl", ttl);
            s.serialize("count", count);
            s.serialize("result", result);
          }

          int request_id;
          /// origin node
          Address originAddress;
          GossipEndpoint originEndpoint;
          /// file address requested
          Address fileAddress;
          int ttl;
          /// number of results we want
          int count;
          /// partial result
          std::vector<GetFileResult> result;
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
          std::vector<GetFileResult> result;
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
          }

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
            s.serialize("id", request_id);
            s.serialize("origin", origin);
            s.serialize("address", fileAddress);
            s.serialize("result", resultAddress);
            s.serialize("resultEndpoint", resultEndpoint);
            s.serialize("ttl", ttl);
          }

          int request_id;
          /// node who created the request
          Address origin;
          Address fileAddress;
          int ttl;
          Address resultAddress;
          RpcEndpoint resultEndpoint;
        };
        REGISTER(PutFileReply, "putReply");

#undef REGISTER
      }

      struct PendingRequest
      {
        std::vector<GetFileResult> result;
        reactor::Barrier barrier;
        Time startTime;
      };

      static inline
      Time
      now()
      {
        return std::chrono::system_clock::now();
      }

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

      template<typename E1, typename E2>
      void
      endpoint_to_endpoint(E1 const& src, E2& dst)
      {
        dst = E2(src.address(), src.port());
      }

      Node::Node(Configuration const& config,
                 bool observer,
                 elle::UUID node_id,
                 infinit::model::doughnut::Doughnut* doughnut)
        : Overlay(std::move(node_id))
        , _config(config)
        , _next_id(1)
        , _observer(observer)
        , _dropped_puts(0)
        , _dropped_gets(0)
        , _failed_puts(0)
      {
        if (observer)
          ELLE_LOG("Running in observer mode");
        this->doughnut(doughnut);
        _remotes_server.listen(0);
        if (_config.bootstrap_nodes.empty())
          _bootstraping.open();
        else
          _bootstraping.close();
        start();
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
        ELLE_TRACE("~Kelips");
        if (_emitter_thread)
          _emitter_thread->terminate_now();
        if (_listener_thread)
          _listener_thread->terminate_now();
        if (_pinger_thread)
          _pinger_thread->terminate_now();
      }

      Address
      address_of_uuid(elle::UUID const& id)
      {
        auto hash = infinit::cryptography::hash(
          elle::ConstWeakBuffer(id.data, id.static_size()),
          infinit::cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      void
      Node::engage()
      {
        _gossip.socket()->close();
        _gossip.bind(GossipEndpoint({}, _port));
        ELLE_LOG("%s: listening on port %s",
                 *this, _gossip.local_endpoint().port());
        ELLE_TRACE("%s: bound to udp, member of group %s", *this, _group);
        _emitter_thread = elle::make_unique<reactor::Thread>("emitter",
          std::bind(&Node::gossipEmitter, this));
        _listener_thread = elle::make_unique<reactor::Thread>("listener",
          std::bind(&Node::gossipListener, this));
        _pinger_thread = elle::make_unique<reactor::Thread>("pinger",
          std::bind(&Node::pinger, this));
        // Send a bootstrap request to bootstrap nodes
        packet::BootstrapRequest req;
        req.sender = address_of_uuid(this->node_id());
        for (auto const& e: _config.bootstrap_nodes)
        {
          ELLE_TRACE("%s: sending bootstrap to node %s", *this, e);
          if (!_config.encrypt || _config.accept_plain)
            send(req, e, Address::null);
          else
          {
            packet::RequestKey req(doughnut()->passport());
            req.sender = address_of_uuid(this->node_id());
            send(req, e, Address::null);
            _pending_bootstrap.push_back(e);
          }
        }
        if (_config.wait)
          wait(_config.wait);
      }

      void Node::start()
      {
        _group = group_of(address_of_uuid(this->node_id()));
        _state.contacts.resize(_config.k);
        // If we are not an observer, we must wait for Local port information
        if (_observer)
          engage();
      }

      void
      Node::setKey(Address const& a, GossipEndpoint const& e,
        infinit::cryptography::SecretKey sk)
      {
        auto oldkey = getKey(a, e);
        if (oldkey)
        {
          ELLE_DEBUG("%s: overriding key %s -> %s  for %s : %s",
            *this, key_hash(*oldkey), key_hash(sk), e, a);
          if (a != Address())
            _keys.find(a)->second = std::move(sk);
          else
            _observer_keys.find(e)->second = std::move(sk);
        }
        else
        {
          if (a != Address())
            _keys.insert(std::make_pair(a, std::move(sk)));
          else
            _observer_keys.insert(std::make_pair(e, std::move(sk)));
        }
      }

      infinit::cryptography::SecretKey*
      Node::getKey(Address const& a, GossipEndpoint const& e)
      {
        if (a == Address())
        {
          auto it = _observer_keys.find(e);
          if (it != _observer_keys.end())
            return &it->second;
        }
        else
        {
          auto it = _keys.find(a);
          if (it != _keys.end())
            return &it->second;
        }
        return nullptr;
      }

      void
      Node::send(packet::Packet& p, GossipEndpoint e, Address a)
      {
        ELLE_ASSERT(e.port() != 0);
        if (this->_observer)
          p.sender = Address::null;
        bool is_crypto = dynamic_cast<const packet::EncryptedPayload*>(&p)
        || dynamic_cast<const packet::RequestKey*>(&p)
        || dynamic_cast<const packet::KeyReply*>(&p);
        elle::Buffer b;
        bool send_key_request = false;
        auto key = getKey(a, e);
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
            ELLE_DEBUG("%s: dropping packet to %s : %s, no key available",
                       *this, e, a);
            send_key_request = true;
          }
          else
          {
            packet::EncryptedPayload ep;
            ep.sender = p.sender;
            ep.encrypt(*key, p);
            b = packet::serialize(ep);
          }
        }
        if (send_key_request)
        {
          packet::RequestKey req(doughnut()->passport());
          req.sender = _observer ? Address::null : address_of_uuid(this->node_id());
          send(req, e, Address::null);
        }
        if (b.size() == 0)
          return;
        static elle::Bench bencher("kelips.packet_size", 5_sec);
        bencher.add(b.size());
        reactor::Lock l(_udp_send_mutex);
        ELLE_DUMP("%s: sending %s bytes packet to %s\n%s", *this, b.size(), e, b.string());
        _gossip.send_to(reactor::network::Buffer(b.contents(), b.size()), e);
      }

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
          static int counter = 1;
          new reactor::Thread(elle::sprintf("process %s", counter++), [buf, source, this]
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
            packet->endpoint = source;
            bool was_crypted = false;
            // First handle crypto related packets
            if (auto p = dynamic_cast<packet::EncryptedPayload*>(packet.get()))
            {
              auto key = getKey(packet->sender, packet->endpoint);
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
                  std::unique_ptr<packet::Packet> plain = p->decrypt(*key);
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
                packet::RequestKey rk(doughnut()->passport());
                rk.sender = address_of_uuid(this->node_id());
                elle::Buffer s = packet::serialize(rk);
                send(rk, source, p->sender);
                return;
              }
            } // EncryptedPayload
            if (auto p = dynamic_cast<packet::RequestKey*>(packet.get()))
            {
              ELLE_DEBUG("%s: processing key request from %s", *this, source);
              // validate passport
              bool ok = p->passport.verify(doughnut()->owner());
              if (!ok)
              {
                ELLE_WARN("%s: failed to validate passport from %s : %s",
                          *this, source, p->sender);
                return;
              }
              auto sk = infinit::cryptography::secretkey::generate(256);
              elle::Buffer password = sk.password();
              setKey(p->sender, p->endpoint, std::move(sk));
              packet::KeyReply kr(doughnut()->passport());
              kr.sender = address_of_uuid(this->node_id());
              kr.encrypted_key = p->passport.user().seal(
                password,
                infinit::cryptography::Cipher::aes256,
                infinit::cryptography::Mode::cbc);
              send(kr, source, p->sender);
              return;
            } // requestkey
            if (auto p = dynamic_cast<packet::KeyReply*>(packet.get()))
            {
              ELLE_DEBUG("%s: processing key reply from %s", *this, source);
              // validate passport
              bool ok = p->passport.verify(doughnut()->owner());
              if (!ok)
              {
                ELLE_WARN("%s: failed to validate passport from %s : %s",
                          *this, source, p->sender);
                return;
              }
              elle::Buffer password = doughnut()->keys().k().open(
                p->encrypted_key,
                infinit::cryptography::Cipher::aes256,
                infinit::cryptography::Mode::cbc);
              infinit::cryptography::SecretKey sk(std::move(password));
              setKey(p->sender, p->endpoint, std::move(sk));
              // Flush operations waiting on crypto ready
              auto it = std::find(_pending_bootstrap.begin(),
                                  _pending_bootstrap.end(),
                                  source);
              if (it != _pending_bootstrap.end())
              {
                ELLE_DEBUG("%s: processing queued operation to %s", *this, source);
                *it = _pending_bootstrap[_pending_bootstrap.size() - 1];
                _pending_bootstrap.pop_back();
                packet::BootstrapRequest req;
                req.sender = address_of_uuid(this->node_id());
                send(req, source, p->sender);
              }
              if (packet->sender != Address::null)
                onContactSeen(packet->sender, source);
              return;
            } // keyreply
            if (!was_crypted && !_config.accept_plain)
            {
              ELLE_WARN("%s: rejecting plain packet from %s : %s",
                        *this, source, packet->sender);
              return;
            }
            if (packet->sender != Address::null)
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
              r.sender = address_of_uuid(this->node_id());
              r.remote_endpoint = source;
              send(r, source, p->sender);
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
            }, true);
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
        std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& res)
      {
        kelips::filterAndInsert(files, target_count, res, _state.contacts[group],
                        &Contact::endpoint, _gen);
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
        std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& res,
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
        // update self file last seen, this will avoid us some ifs at other places
        for (auto& f: _state.files)
        {
          if (f.second.home_node == address_of_uuid(this->node_id()))
          {
            f.second.last_seen = now();
          }
        }
        std::unordered_multimap<Address, std::pair<Time, Address>> res;
        unsigned int max_new = _config.gossip.files / 2;
        unsigned int max_old = _config.gossip.files / 2;
        // insert new files
        std::vector<std::pair<Address, std::pair<Time, Address>>> new_files;
        for (auto const& f: _state.files)
        {
          if (f.second.gossip_count < _config.gossip.new_threshold)
            new_files.push_back(std::make_pair(f.first,
              std::make_pair(f.second.last_seen, f.second.home_node)));
        }
        if (new_files.size() > max_new)
        {
          if (max_new < new_files.size() - max_new)
            new_files = pick_n(new_files, max_new, _gen);
          else
            new_files = remove_n(new_files, new_files.size() - max_new, _gen);
        }
        for (auto const& nf: new_files)
        {
          res.insert(nf);
        }
        // insert old files, only our own for which we can update the last_seen value
        std::vector<std::pair<Address, std::pair<Time, Address>>> old_files;
        for (auto& f: _state.files)
        {
          if (f.second.home_node == address_of_uuid(this->node_id())
            && ((now() - f.second.last_gossip) > std::chrono::milliseconds(_config.gossip.old_threshold_ms))
            && !has(res, f.first, f.second.home_node))
              old_files.push_back(std::make_pair(f.first, std::make_pair(f.second.last_seen, f.second.home_node)));
        }
        if (old_files.size() > max_old)
        {
          if (max_old < old_files.size() - max_old)
            old_files = pick_n(old_files, max_old, _gen);
          else
            old_files = remove_n(old_files, old_files.size() - max_old, _gen);
        }
        for (auto const& nf: old_files)
        {
          res.insert(nf);
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
        p.sender = address_of_uuid(this->node_id());
        while (true)
        {
          reactor::sleep(boost::posix_time::millisec(_config.gossip.interval_ms));
          p.contacts.clear();
          p.files.clear();
          p.contacts = pickContacts();
          elle::Buffer buf = serialize(p);
          auto targets = pickOutsideTargets();
          for (auto const& e: targets)
            send(p, e.first, e.second);
          // Add some files, just for group targets
          p.files = pickFiles();
          buf = serialize(p);
          targets = pickGroupTargets();
          if (p.files.size() && targets.empty())
            ELLE_TRACE("%s: have files but no group member known", *this);
          for (auto const& e: targets)
          {
            if (!p.files.empty())
              ELLE_TRACE("%s: info on %s files %s   %x %x", *this, p.files.size(),
                       serialize_time(p.files.begin()->second.first),
                       address_of_uuid(this->node_id()), p.files.begin()->second.second);
            send(p, e.first, e.second);
          }
        }
      }

      void
      Node::onContactSeen(Address addr, GossipEndpoint endpoint)
      {
        // Drop self
        if (addr == address_of_uuid(this->node_id()))
          return;
        int g = group_of(addr);
        if (g == _group)
          _bootstraping.open();
        Contacts& target = _state.contacts[g];
        auto it = target.find(addr);
        if (it == target.end())
        {
          if (g == _group || signed(target.size()) < _config.max_other_contacts)
          {
            Contact contact{endpoint, addr, Duration(0), now(), Time(), 0};
            ELLE_LOG("%s: register %s", *this, contact);
            target[addr] = std::move(contact);
          }
        }
        else
        {
          it->second.last_seen = now();
          if (it->second.endpoint != endpoint)
          {
            it->second.endpoint = endpoint;
            ELLE_LOG("%s: re-register %s", *this, it->second);
          }
          else
            ELLE_DUMP("%s: just seen %s", *this, it->second);
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

      void
      Node::onGossip(packet::Gossip* p)
      {
        ELLE_TRACE("%s: rocessing gossip from %s", *this, p->endpoint);
        int g = group_of(p->sender);
        if (g != _group && !p->files.empty())
          ELLE_WARN("%s: Received files from another group: %s at %s", *this, p->sender, p->endpoint);
        for (auto const& c: p->contacts)
        {
          if (c.first == address_of_uuid(this->node_id()))
            continue;
          int g = group_of(c.first);
          auto& target = _state.contacts[g];
          auto it = target.find(c.first);
          if (it == target.end())
          {
            if (g == _group || target.size() < (unsigned)_config.max_other_contacts)
              target[c.first] = Contact{c.second.second, c.first, Duration(), c.second.first, Time(), 0};
          }
          else if (it->second.last_seen < c.second.first)
          { // Also update endpoint in case it changed
            if (it->second.endpoint != c.second.second)
              ELLE_DEBUG(
                "%s: %s endpoint change from %s to %s",
                *this, it->first, it->second.endpoint, c.second.second);
            it->second.last_seen = c.second.first;
            it->second.endpoint = c.second.second;
          }
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
              ELLE_TRACE("%s: registering %x live since %s (%s)", *this,
                         f.first,
                         std::chrono::duration_cast<std::chrono::seconds>(now() - f.second.first).count(),
                         (now() - f.second.first).count());
            }
            else
            {
              ELLE_TRACE("%s: %s %s %s %x", *this,
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
      Node::onBootstrapRequest(packet::BootstrapRequest* p)
      {
        int g = group_of(p->sender);
        packet::Gossip res;
        res.sender = address_of_uuid(this->node_id());
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
        send(res, p->endpoint, p->sender);
      }

      void
      Node::addLocalResults(packet::GetFileRequest* p)
      {
        int fg = group_of(p->fileAddress);
        auto its = _state.files.equal_range(p->fileAddress);
        // Shuffle the match list
        std::vector<decltype(its.first)> iterators;
        for (auto it = its.first; it != its.second; ++it)
          iterators.push_back(it);
        std::shuffle(iterators.begin(), iterators.end(), _gen);
        for (auto iti = iterators.begin(); iti != iterators.end()
          && p->result.size() < unsigned(p->count); ++iti)
        {
          auto it = *iti;
          // Check if this one is already in results
          if (std::find_if(p->result.begin(), p->result.end(),
            [&](GetFileResult const& r) -> bool {
              return r.first == it->second.home_node;
            }) != p->result.end())
          continue;
          // find the corresponding endpoint
          RpcEndpoint endpoint;
          bool found = false;
          if (it->second.home_node == address_of_uuid(this->node_id()))
          {
            ELLE_DEBUG("%s: found self", *this);
            if (_local_endpoint.address().to_string() == "0.0.0.0")
            {
              ELLE_TRACE("Endpoint yet unknown, assuming localhost");
              endpoint = RpcEndpoint(
                boost::asio::ip::address::from_string("127.0.0.1"),
                this->_port);
            }
            else
              endpoint_to_endpoint(_local_endpoint, endpoint);
            found = true;
          }
          else
          {
            auto contact_it = _state.contacts[fg].find(it->second.home_node);
            if (contact_it != _state.contacts[fg].end())
            {
              ELLE_DEBUG("%s: found other", *this);
              endpoint_to_endpoint(contact_it->second.endpoint, endpoint);
              found = true;
            }
            else
              ELLE_LOG("%s: have file but not node", *this);
          }
          if (!found)
            continue;
          GetFileResult res;
          res.first = it->second.home_node;
          endpoint_to_endpoint(endpoint, res.second);
          p->result.push_back(res);
        }
      }

      void
      Node::onGetFileRequest(packet::GetFileRequest* p)
      {
        ELLE_TRACE("%s: getFileRequest %s/%x %s/%s", *this, p->request_id, p->fileAddress,
                 p->result.size(), p->count);
        if (p->originEndpoint.port() == 0)
          p->originEndpoint = p->endpoint;
        int fg = group_of(p->fileAddress);
        if (fg == _group)
        {
          addLocalResults(p);
        }
        if (p->result.size() >= unsigned(p->count))
        { // We got the full result, send reply
          packet::GetFileReply res;
          res.sender = address_of_uuid(this->node_id());
          res.fileAddress = p->fileAddress;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          res.result = p->result;
          res.ttl = p->ttl;
          ELLE_TRACE("%s: replying to %s/%s", *this, p->originEndpoint, p->request_id);
          send(res, p->originEndpoint, p->originAddress);
          // FIXME: should we route the reply back the same path?
          return;
        }
        ELLE_TRACE("%s: route %s", *this, p->ttl);
        // We don't have it, route the request,
        if (p->ttl == 0)
        {
          // FIXME not in initial protocol, but we cant distinguish get and put
          packet::GetFileReply res;
          res.sender = address_of_uuid(this->node_id());
          res.fileAddress = p->fileAddress;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          res.result = p->result;
          res.ttl = 1;
          send(res, p->originEndpoint, p->originAddress);
          _dropped_gets++;
          return;
        }
        p->ttl--;
        p->sender = address_of_uuid(this->node_id());
        int count = _state.contacts[fg].size();
        if (count == 0)
          return;
        std::uniform_int_distribution<> random(0, count-1);
        int idx = random(_gen);
        auto it = _state.contacts[fg].begin();
        while (idx--) ++it;
        send(*p, it->second.endpoint, it->second.address);
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

      void
      Node::onPutFileRequest(packet::PutFileRequest* p)
      {
        ELLE_TRACE("%s: putFileRequest %s %s %x", *this, p->ttl, p->insert_ttl, p->fileAddress);
        if (p->originEndpoint.port() == 0)
          p->originEndpoint = p->endpoint;
        if (p->insert_ttl == 0)
        {
          // check if we didn't already accept this file
          if (std::find(_promised_files.begin(), _promised_files.end(), p->fileAddress)
            == _promised_files.end())
          {
            // Check if we already have the block
            auto its = _state.files.equal_range(p->fileAddress);
            auto it = std::find_if(its.first, its.second,
              [&](decltype(*its.first) i) ->bool {
                return i.second.home_node == address_of_uuid(this->node_id());
              });
            if (it == its.second)
            { // Nope, insert here
              // That makes us a home node for this address, but
              // wait until we get the RPC to store anything
              packet::PutFileReply res;
              res.sender = address_of_uuid(this->node_id());
              res.request_id = p->request_id;
              res.origin = p->originAddress;
              endpoint_to_endpoint(_local_endpoint, res.resultEndpoint);
              res.fileAddress = p->fileAddress;
              res.resultAddress = address_of_uuid(this->node_id());
              res.ttl = p->ttl;
              _promised_files.push_back(p->fileAddress);
              send(res, p->originEndpoint, p->originAddress);
              return;
            }
          }
        }
        // Forward
        if (p->insert_ttl > 0)
          p->insert_ttl--;
        if (p->ttl == 0)
        {
          packet::PutFileReply res;
          res.sender = address_of_uuid(this->node_id());
          res.request_id = p->request_id;
          res.origin = p->originAddress;
          res.fileAddress = p->fileAddress;
          res.resultAddress = Address::null;
          res.ttl = 0;
          send(res, p->originEndpoint, p->originAddress);
          ELLE_TRACE("%s: reporting failed putfile request for %x", *this, p->fileAddress);
          _dropped_puts++;
          return;
        }
        // Forward the packet to an other node
        int fg = group_of(p->fileAddress);
        auto it = random_from(_state.contacts[fg], _gen);
        if (it == _state.contacts[fg].end())
          it = random_from(_state.contacts[_group], _gen);
        if (it == _state.contacts[_group].end())
        {
          ELLE_ERR("%s: No contact founds", *this);
          return;
        }
        p->sender = address_of_uuid(this->node_id());
        p->ttl--;
        send(*p, it->second.endpoint, it->second.address);
      }

      void
      Node::onPutFileReply(packet::PutFileReply* p)
      {
        ELLE_DEBUG("%s: got reply for %x: %s", *this, p->fileAddress, p->resultAddress);
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
        ELLE_DEBUG("%s: unlocking waiter on response %s: %s", *this, p->request_id,
                   p->resultAddress);
        it->second->result.push_back(std::make_pair(p->resultAddress, p->resultEndpoint));
        it->second->barrier.open();
        _pending_requests.erase(it);
      }

      std::vector<RpcEndpoint>
      Node::address(Address file,
                    infinit::overlay::Operation op,
                    int n)
      {
        if (op == infinit::overlay::OP_INSERT)
          return kelipsPut(file, n);
        else if (op == infinit::overlay::OP_INSERT_OR_UPDATE)
        {
          try
          {
            return kelipsGet(file, n);
          }
          catch (reactor::Timeout const& e)
          {
            ELLE_TRACE("%s: get failed on %x, trying put", *this, file);
            ELLE_TRACE("%s", e.backtrace());
            return kelipsPut(file, n);
          }
        }
        else
        {
          try
          {
            return kelipsGet(file, n);
          }
          catch (reactor::Timeout const& e)
          {
            throw infinit::model::MissingBlock(file);
          }
        }
      }

      std::vector<RpcEndpoint>
      Node::kelipsGet(Address file, int n)
      {
        std::set<RpcEndpoint> result_set;
        packet::GetFileRequest r;
        r.sender = address_of_uuid(this->node_id());
        r.request_id = ++ _next_id;
        r.originAddress = _observer ? Address::null : address_of_uuid(this->node_id());
        r.originEndpoint = _local_endpoint;
        r.fileAddress = file;
        r.ttl = _config.query_get_ttl;
        r.count = n;
        int fg = group_of(file);
        if (fg == _group)
        {
          // check if we have it locally
          auto its = _state.files.equal_range(file);
          auto it_us = std::find_if(its.first, its.second,
            [&](std::pair<const infinit::model::Address, File> const& f) {
              return f.second.home_node == address_of_uuid(this->node_id());
          });
          if (it_us != its.second && n == 1)
          {
            ELLE_DEBUG("Satisfied get lookup locally.");
            std::vector<RpcEndpoint> result;
            result.emplace_back(boost::asio::ip::address::from_string("127.0.0.1"),
                this->_port);
            return result;
          }
          // add result for our own file table
          addLocalResults(&r);
          for (auto const& e: r.result)
            result_set.insert(e.second);
          if (result_set.size() >= unsigned(n))
          { // Request completed locally
            std::vector<RpcEndpoint> result(result_set.begin(), result_set.end());
            return result;
          }
        }
        for (int i=0; i < _config.query_get_retries; ++i)
        {
          packet::GetFileRequest req(r);
          req.request_id = ++_next_id;
          auto r = std::make_shared<PendingRequest>();
          r->startTime = now();
          r->barrier.close();
          _pending_requests[req.request_id] = r;
          // Select target node
          auto it = random_from(_state.contacts[fg], _gen);
          if (it == _state.contacts[fg].end())
            it = random_from(_state.contacts[_group], _gen);
          if (it == _state.contacts[_group].end())
          {
            ELLE_TRACE("No contact to forward GET to");
            if (result_set.empty())
              throw reactor::Timeout(boost::posix_time::milliseconds(
                _config.query_timeout_ms * _config.query_get_retries));
            std::vector<RpcEndpoint> result(result_set.begin(), result_set.end());
            return result;
          }
          ELLE_DEBUG("%s: get request %s(%s)", *this, i, req.request_id);
          send(req, it->second.endpoint, it->second.address);
          try
          {
            reactor::wait(r->barrier,
              boost::posix_time::milliseconds(_config.query_timeout_ms));
          }
          catch (reactor::Timeout const& t)
          {
            ELLE_LOG("%s: Timeout on attempt %s", *this, i);
          }
          ELLE_DEBUG("%s: request %s(%s) gave %s results", *this, i, req.request_id,
                     r->result.size());
          for (auto const& e: r->result)
            result_set.insert(e.second);
          if (signed(result_set.size()) >= n)
            break;
        }
        if (result_set.empty())
          throw reactor::Timeout(boost::posix_time::milliseconds(
            _config.query_timeout_ms * _config.query_get_retries));
        std::vector<RpcEndpoint> result(result_set.begin(), result_set.end());
        return result;
      }

      std::vector<RpcEndpoint>
      Node::kelipsPut(Address file, int n)
      {
        int fg = group_of(file);
        packet::PutFileRequest p;
        p.sender = address_of_uuid(this->node_id());
        p.originAddress = _observer ? Address::null : address_of_uuid(this->node_id());
        p.originEndpoint = _local_endpoint;
        p.fileAddress = file;
        p.ttl = _config.query_put_ttl;
        p.count = 1;
        p.insert_ttl = _config.query_put_insert_ttl;
        std::vector<RpcEndpoint> results;
        auto make_one = [&] {
          for (int i=0; i< _config.query_put_retries; ++i)
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
                throw std::runtime_error("No contacts in self/target groups");
              // Bootstraping only: Store locally if not already there
              bool have_file = std::find_if(_state.files.begin(), _state.files.end(),
                [&] (Files::value_type const& v)
              {
                return v.second.address == file
                  && v.second.home_node == address_of_uuid(this->node_id());
              }) != _state.files.end();
              if (_config.bootstrap_nodes.empty() && !have_file && std::find(_promised_files.begin(), _promised_files.end(), p.fileAddress)
                == _promised_files.end())
              {
                _promised_files.push_back(p.fileAddress);
                results.push_back(RpcEndpoint(
                  boost::asio::ip::address::from_string("127.0.0.1"),
                  this->_port));
                return;
              }
              else
                return;
            }
            ELLE_DEBUG("%s: put request %s(%s)", *this, i, req.request_id);
            send(req, it->second.endpoint, it->second.address);
            try
            {
              reactor::wait(r->barrier,
                boost::posix_time::milliseconds(_config.query_timeout_ms));
            }
            catch (reactor::Timeout const& t)
            {
              ELLE_LOG("%s: Timeout on attempt %s", *this, i);
            }
            if (r->barrier.opened())
            {
              ELLE_ASSERT(r->result.size());
              if (r->result.front().first != Address::null)
              {
                results.push_back(r->result.front().second);
                return;
              }
            }
            ELLE_TRACE("%s: put failed, retry %s", *this, i);
            ++_failed_puts;
          }
        };
        elle::With<reactor::Scope>() << [&](reactor::Scope& s)
        {
          for (int i=0; i<n; ++i)
            s.run_background("put", make_one);
          s.wait();
        };
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

      std::vector<std::pair<GossipEndpoint, Address>>
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
        std::vector<std::pair<GossipEndpoint, Address>> res;
        for (auto const& a: addresses)
        {
          int i = group_of.at(a);
          res.push_back(std::make_pair(_state.contacts[i].at(a).endpoint, a));
        }
        return res;
      }

      std::vector<std::pair<GossipEndpoint, Address>>
      Node::pickGroupTargets()
      {
        std::map<Address, Duration> candidates;
        for (auto const& e: _state.contacts[_group])
          candidates[e.first] = e.second.rtt;
        std::vector<Address> r = pick(candidates, _config.gossip.group_target, _gen);
        std::vector<std::pair<GossipEndpoint, Address>> result;
        for (auto const& a: r)
          result.push_back(std::make_pair(_state.contacts[_group].at(a).endpoint, a));
        return result;
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
          std::stringstream ss;
          ss << "g: " << _group << "  f: " << _state.files.size() << "  c:";
          for(auto const& c: _state.contacts)
            ss << c.size() << ' ';
          ELLE_TRACE("%s: %s", *this, ss.str());
          // pick a target
          GossipEndpoint endpoint;
          Address address;
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
            address = it->second.address;
            _ping_target = it->first;
            break;
          }
          packet::Ping p;
          p.sender = address_of_uuid(this->node_id());
          p.remote_endpoint = endpoint;
          _ping_time = now();
          ELLE_DEBUG("%s: pinging %x at %s", *this, _ping_target, endpoint);
          send(p, endpoint, address);
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

      void
      Node::cleanup()
      {
        auto it = _state.files.begin();
        auto t = now();
        auto file_timeout = std::chrono::milliseconds(_config.file_timeout_ms);
        while (it != _state.files.end())
        {
          if (!(it->second.home_node == address_of_uuid(this->node_id())) &&
              t - it->second.last_seen > file_timeout)
          {
            ELLE_TRACE("%s: erase file %x", *this, it->first);
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
            if (t - it->second.last_seen > file_timeout)
            {
              ELLE_LOG("%s: erase %s", *this, it->second);
              it = contacts.erase(it);
            }
            else
              ++it;
          }
        }
        int my_files = 0;
        for (auto const& f: _state.files)
        {
          if (f.second.home_node == address_of_uuid(this->node_id()))
            ++my_files;
        }
        int time_send_all = my_files / (_config.gossip.files/2 ) *  _config.gossip.interval_ms;
        ELLE_DEBUG("time_send_all is %s", time_send_all);
        if (time_send_all >= _config.file_timeout_ms / 2)
        {
          ELLE_TRACE_SCOPE(
            "%s: too many files for configuration: "
            "files=%s, per packet=%s, interval=%s, timeout=%s",
            *this, my_files, _config.gossip.files,
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
      Node::register_local(std::shared_ptr<infinit::model::doughnut::Local> l)
      {
        ELLE_ASSERT(!this->_observer);
        this->_local = l;
        l->on_fetch.connect(std::bind(&Node::fetch, this,
                                          std::placeholders::_1,
                                          std::placeholders::_2));
        l->on_store.connect(std::bind(&Node::store, this,
                                          std::placeholders::_1,
                                          std::placeholders::_2));
        l->on_remove.connect(std::bind(&Node::remove, this,
                                           std::placeholders::_1));
        this->_port = l->server_endpoint().port();
        reload_state(*l);
        this->engage();
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
            return f.second.home_node == address_of_uuid(this->node_id());
        }) == its.second)
          _state.files.insert(std::make_pair(block.address(),
            File{block.address(), address_of_uuid(this->node_id()), now(), Time(), 0}));
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
          if (it->second.home_node == address_of_uuid(this->node_id()))
          {
            _state.files.erase(it);
            break;
          }
        }
      }

      Node::Members
      Node::_lookup(infinit::model::Address address,
                    int n,
                    infinit::overlay::Operation op) const
      {
        if (op != infinit::overlay::Operation::OP_FETCH)
        {
          ELLE_TRACE("Waiting for bootstrap");
          reactor::wait(const_cast<Node*>(this)->_bootstraping);
          ELLE_TRACE("bootstrap opened");
        }
        Members res;
        auto hosts = const_cast<Node*>(this)->address(address, op, n);
        ELLE_TRACE("address produced %s hosts:", hosts.size());
        for (auto const& host: hosts)
        {
          ELLE_TRACE("connecting to %s", host);
          if (host.address().to_string() == "127.0.0.1" && host.port() == _port)
          {
            res.emplace_back(_local);
            continue;
          }
          using Protocol = infinit::model::doughnut::Local::Protocol;
          if (_config.rpc_protocol == Protocol::utp || _config.rpc_protocol == Protocol::all)
          {
            try
            {
              res.emplace_back(
                new infinit::model::doughnut::Remote(
                  const_cast<infinit::model::doughnut::Doughnut&>(*this->doughnut()),
                  boost::asio::ip::udp::endpoint(host.address(), host.port()+100),
                  const_cast<Node*>(this)->_remotes_server));
              continue;
            }
            catch (reactor::Terminate const& e)
            {
              throw;
            }
            catch (std::exception const& e)
            {
              ELLE_WARN("Failed to connect with utp to node %s", host);
            }
          }
          if (_config.rpc_protocol == Protocol::tcp || _config.rpc_protocol == Protocol::all)
          {
            try
            {
              res.emplace_back(
                new infinit::model::doughnut::Remote(
                  const_cast<infinit::model::doughnut::Doughnut&>(*this->doughnut()),
                  host));
              continue;
            }
            catch (reactor::Terminate const& e)
            {
              throw;
            }
            catch (std::exception const& e)
            {
              ELLE_WARN("Failed to connect with tcp to node %s", host);
            }
          }
        }
        return res;
      }

      void
      Node::print(std::ostream& stream) const
      {
        stream << "Kelips(" << _local_endpoint << ')';
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
            File{k, address_of_uuid(this->node_id()), now(), Time(), 0}));
          ELLE_DEBUG("%s: reloaded %x", *this, k);
        }
      }

      elle::json::Json
      Node::stats()
      {
        elle::json::Object res;
        res["group_contacts"] = _state.contacts[_group].size();
        res["dropped_puts"] = _dropped_puts;
        res["dropped_gets"] = _dropped_gets;
        res["failed_puts"] = _failed_puts;
        return res;
      }

      std::ostream&
      operator << (std::ostream& output, Contact const& contact)
      {
        elle::fprintf(output, "contact %x at %s",
                      contact.address, contact.endpoint);
        return output;
      }

      Configuration::Configuration()
        : overlay::Configuration()
        , k(6)
        , max_other_contacts(6)
        , query_get_retries(30)
        , query_put_retries(12)
        , query_timeout_ms(1000)
        , query_get_ttl(10)
        , query_put_ttl(10)
        , query_put_insert_ttl(3)
        , contact_timeout_ms(120000)
        , file_timeout_ms(120000)
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
        s.serialize("bootstrap_nodes", bootstrap_nodes,
                    elle::serialization::as<PrettyGossipEndpoint>());
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
      Configuration::make(std::vector<std::string> const& hosts, bool server,
                          infinit::model::doughnut::Doughnut* doughnut)
      {
        for (auto const& host: hosts)
          this->bootstrap_nodes.push_back(PrettyGossipEndpoint(host));
        return elle::make_unique<Node>(
          *this, !server, this->node_id(), doughnut);
      }

      static const
      elle::serialization::Hierarchy<infinit::overlay::Configuration>::
      Register<Configuration> _registerKelipsOverlayConfig("kelips");
    }
  }
}
