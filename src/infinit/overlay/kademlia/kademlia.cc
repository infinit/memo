#include <infinit/overlay/kademlia/kademlia.hh>

#include <elle/log.hh>

#include <cryptography/oneway.hh>
#include <reactor/network/buffer.hh>
#include <reactor/network/exception.hh>

extern "C" {
#include <infinit/overlay/kademlia/dht/dht.h>
}

ELLE_LOG_COMPONENT("infinit.overlay.kademlia");


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
      static kademlia::PrettyEndpoint convert(std::string& repr)
      {
        size_t sep = repr.find_first_of(':');
        auto addr = boost::asio::ip::address::from_string(repr.substr(0, sep));
        int port = std::stoi(repr.substr(sep + 1));
        return kademlia::PrettyEndpoint(addr, port);
      }
    };
  }
}

static std::default_random_engine gen;

static void on_event(void *closure, int event,
             unsigned char *info_hash,
             void *data, size_t data_len)
{
  ((kademlia::Kademlia*)closure)->on_event(event, info_hash, data, data_len);
}

namespace kademlia
{


  Kademlia::Kademlia(Configuration const& config,
                     std::unique_ptr<infinit::storage::Storage> storage)
  : Local(std::move(storage), config.port)
  , _config(config)
  {
    dht_debug = fdopen(2, "w");
    _socket.socket()->close();
    _socket.bind(Endpoint({}, config.port));
    dht_init(_socket.socket()->native_handle(), -1, config.node_id.value(), 0);
    for (auto const& e: _config.bootstrap_nodes)
    {
      dht_ping_node(const_cast<sockaddr*>(e.data()), e.size());
    }
    _looper = elle::make_unique<reactor::Thread>("looper",
      [this] { this->_loop();});
    if (config.wait)
    {
      while (true)
      {
        int g,d,c,i;
        dht_nodes(AF_INET, &g, &d, &c, &i);
        ELLE_TRACE("Waiting for %s nodes, got %s", config.wait, g+d);
        if (g+d > config.wait)
          break;
        reactor::sleep(1_sec);
      }
    }
  }

  void Kademlia::_loop()
  {
    unsigned char buffer[4096];
    time_t sleepTime = 1;
    while (true)
    {
      try
      {
        Endpoint ep;
        ELLE_DEBUG("recvfrom delay %s", sleepTime);
        size_t sz = _socket.receive_from(
          reactor::network::Buffer(buffer, 4096),
          ep,
          boost::posix_time::seconds(sleepTime));
        ELLE_DEBUG("recvfrom got %s bytes from %s", sz, ep);
        dht_periodic(buffer, sz, ep.data(), ep.size(), &sleepTime, ::on_event, this);
      }
      catch (reactor::network::TimeOut const& to)
      {
        dht_periodic(0, 0, 0, 0, &sleepTime, ::on_event, this);
      }
    }
  }

  // copied from dht
  struct search_node {
    unsigned char id[20];
    struct sockaddr_storage ss;
    int sslen;
    time_t request_time;        /* the time of the last unanswered request */
    time_t reply_time;          /* the time of the last reply */
    int pinged;
    unsigned char token[40];
    int token_len;
    int replied;                /* whether we have received a reply */
    int acked;                  /* whether they acked our announcement */
  };

  void Kademlia::on_event(int event, unsigned char *info_hash,
                          void *data, size_t data_len)
  {
    ELLE_TRACE("on event %s", event);
    std::string key((char*)info_hash, 20);
    auto req = _requests.at(key);
    if (event == DHT_EVENT_VALUES)
    {
      unsigned char* cdata = (unsigned char*)data;
      int count = data_len / 6;
      for (int i=0; i<count; ++i)
      {
        if (req->result.size() >= unsigned(req->n))
          break;
        sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        memcpy(&sin.sin_addr, cdata + i*6, 4);
        memcpy(&sin.sin_port, cdata + i*6 + 2, 2);
        boost::asio::ip::tcp::endpoint e;
        memcpy(e.data(), &sin, e.size());
        ELLE_TRACE("Adding to result: %s", e);
        req->result.push_back(e);
      }
    }
    else if (event == DHT_EVENT_SEARCH_DONE)
    {
      if ( (req->op == infinit::overlay::OP_INSERT_OR_UPDATE && req->result.empty())
        || req->op == infinit::overlay::OP_INSERT)
      { // No result of course, but we hacked in dht to give us
        // closest node addresses;
        search_node* n = (search_node*)data;
        int count = data_len;
        for (int i=0; i<count; ++i)
        {
          if (req->result.size() >= unsigned(req->n))
            break;
          boost::asio::ip::tcp::endpoint e;
          memcpy(e.data(), &n[i].ss, e.size());
          ELLE_TRACE("Adding close node to result: %s", e);
        }
      }
      req->barrier.open();
    }
  }

  infinit::overlay::Overlay::Members Kademlia::_lookup(infinit::model::Address address,
                                     int n, infinit::overlay::Operation op) const
  {
    ELLE_TRACE("lookup %x", address);
    std::string key((const char*)address.value(), 20);
    auto req = std::make_shared<PendingRequest>();
    req->op = op;
    req->n = n;
    _requests[key] = req;
    req->barrier.close();
    dht_search((const unsigned char*)key.data(), 0, AF_INET, ::on_event, (void*)this);
    req->barrier.wait();
    ELLE_TRACE("lookup %x finished with %s", address, req->result.size());
    return req->result;
  }

  void Configuration::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("node_id", node_id);
    s.serialize("port", port);
    s.serialize("bootstrap_nodes", bootstrap_nodes);
    s.serialize("wait", wait);
  }

  void Kademlia::store(infinit::model::blocks::Block const& block,
                       infinit::model::StoreMode mode)
  {
    Local::store(block, mode);
    // advertise it
    dht_search(block.address().value(), server_endpoint().port(), AF_INET, 0, 0);
  }
  void Kademlia::remove(Address address)
  {
    Local::remove(address);
  }
  std::unique_ptr<infinit::model::blocks::Block>
  Kademlia::fetch(Address address) const
  {
    return Local::fetch(address);
  }

}

extern "C" {
  int dht_blacklisted(const struct sockaddr *sa, int salen)
  {
    return 0;
  }
  void dht_hash(void *hash_return, int hash_size,
                const void *v1, int len1,
                const void *v2, int len2,
                const void *v3, int len3)
  {
    elle::Buffer buf;
    buf.append(v1, len1);
    buf.append(v2, len2);
    buf.append(v3, len3);
    using namespace infinit::cryptography;
    auto res = oneway::hash(Plain(buf), oneway::Algorithm::sha256);
    ELLE_ASSERT(signed(res.buffer().size()) >= hash_size);
    memcpy(hash_return, res.buffer().contents(), hash_size);
  }
  int dht_random_bytes(void *buf, size_t size)
  {
    unsigned char* cbuf = (unsigned char*)buf;
    auto dist = std::uniform_int_distribution<>(0, 255);
    for (unsigned i=0; i<size; ++i)
      cbuf[i] = dist(gen);
    return 0;
  }
}