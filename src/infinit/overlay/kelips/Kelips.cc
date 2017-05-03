#include <infinit/overlay/kelips/Kelips.hh>

#include <algorithm>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/cxx11/none_of.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <elle/algorithm.hh>
#include <elle/bench.hh>
#include <elle/make-vector.hh>
#include <elle/network/Interface.hh>
#include <elle/os/environ.hh>
#include <elle/range.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>
#include <elle/serialization/json.hh>
#include <elle/utils.hh>

#include <elle/cryptography/SecretKey.hh>
#include <elle/cryptography/Error.hh>
#include <elle/cryptography/hash.hh>
#include <elle/cryptography/random.hh>

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/network/resolve.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

#include <infinit/storage/Filesystem.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Passport.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh> // FIXME

ELLE_LOG_COMPONENT("infinit.overlay.kelips");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.kelips." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

using Serializer = elle::serialization::Binary;

using boost::algorithm::any_of;
using boost::algorithm::any_of_equal;
using boost::algorithm::none_of;
using boost::algorithm::none_of_equal;
using boost::algorithm::starts_with;

namespace elle
{
  namespace kelips = infinit::overlay::kelips;

  namespace serialization
  {

    template<> struct Serialize<kelips::Time>
    {
      using Type = uint64_t;
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

struct PrettyEndpoint
{
  using Endpoint = infinit::overlay::kelips::Endpoint;
  PrettyEndpoint(Endpoint const& e)
    : _repr(elle::sprintf("%s:%s", e.address(), e.port()))
  {}

  PrettyEndpoint(elle::serialization::Serializer& input)
  {
    this->serialize(input);
  }

  PrettyEndpoint(std::string repr)
    : _repr(std::move(repr))
  {}

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize_forward(this->_repr);
  }

  operator Endpoint()
  {
    size_t sep = this->_repr.find_last_of(':');
    auto a = boost::asio::ip::address::from_string(this->_repr.substr(0, sep));
    int p = std::stoi(this->_repr.substr(sep + 1));
    return {a, p};
  }

  ELLE_ATTRIBUTE_R(std::string, repr);
};

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
      namespace
      {
        inline
        Time
        now()
        {
          return std::chrono::system_clock::now();
        }

        void
        endpoints_update(std::vector<TimedEndpoint>& endpoints, Endpoint entry,
                         Time t = now())
        {
          auto hit = boost::find_if(endpoints,
              [&](TimedEndpoint const& e) { return e.first == entry;});
          if (hit == endpoints.end())
            endpoints.emplace_back(entry, now());
          else
            hit->second = std::max(t, hit->second);
        }

        void
        endpoints_update(std::vector<TimedEndpoint>& endpoints,
                         const std::vector<TimedEndpoint>& src)
        {
          for (auto const& te: src)
            endpoints_update(endpoints, te.first, te.second);
        }

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

        Time
        endpoints_max(std::vector<TimedEndpoint> const& endpoints)
        {
          return boost::max_element(endpoints,
            [](TimedEndpoint const& a, TimedEndpoint const& b) {
              return a.second < b.second;
            })->second;
        }

        Endpoints
        to_endpoints(std::vector<TimedEndpoint> const& v)
        {
          auto res = Endpoints{};
          for (auto const& e: v)
            res.emplace(e.first);
          return res;
        }

        uint64_t
        serialize_time(const Time& t)
        {
          return elle::serialization::Serialize<Time>::convert(
            const_cast<Time&>(t));
        }

        std::string
        key_hash(elle::cryptography::SecretKey const& k)
        {
          auto hk = elle::cryptography::hash(k.password(),
                                                 elle::cryptography::Oneway::sha256);
          std::string hkhex = elle::sprintf("%x", hk);
          return hkhex.substr(0,3) + hkhex.substr(hkhex.length()-3);
        }

        void
        merge_peerlocations(NodeLocations& dst, NodeLocations const& src)
        {
          for (auto const& r: src)
            {
              auto it = boost::find_if(dst,
                                       [&](const NodeLocation& a)
                                       {
                                         return a.id() == r.id();
                                       });
            if (it == dst.end())
              dst.push_back(r);
            else
              // FIXME: check for some union algorithm.
              for (auto const& ep: r.endpoints())
                it->endpoints().insert(ep);
          }
        }
      }

      namespace packet
      {
        static bool disable_compression = elle::os::inenv("KELIPS_DISABLE_COMPRESSION");
        struct CompressPeerLocations{};

        template<typename T>
        elle::Buffer
        serialize(T const& packet, model::doughnut::Doughnut& dn)
        {
          ELLE_ASSERT(&packet);
          elle::Buffer buf;
          elle::IOStream stream(buf.ostreambuf());
          Serializer::SerializerOut output(stream, false);
          output.set_context(&dn);
          if (dn.version() >= elle::Version(0, 7, 0) && !disable_compression)
            output.set_context(CompressPeerLocations{});
          auto ptr = &(packet::Packet&)packet;
          output.serialize_forward(ptr);
          return buf;
        }

#define REGISTER(classname, type)                               \
        static const elle::serialization::Hierarchy<Packet>::   \
        Register<classname>                                     \
        _registerPacket##classname(type)

        struct Packet
          : public elle::serialization::VirtuallySerializable<Packet, false>
        {
          Endpoint endpoint;
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
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("payload", payload);
          }

          std::unique_ptr<Packet>
          decrypt(elle::cryptography::SecretKey const& k,
                  model::doughnut::Doughnut& dn)
          {
            elle::Buffer plain = k.decipher(
              payload,
              elle::cryptography::Cipher::aes256,
              elle::cryptography::Mode::cbc,
              elle::cryptography::Oneway::sha256);
            elle::IOStream stream(plain.istreambuf());
            Serializer::SerializerIn input(stream, false);
            if (dn.version() >= elle::Version(0, 7, 0) && !disable_compression)
              input.set_context(CompressPeerLocations{});
            input.set_context(&dn);
            auto res = std::unique_ptr<packet::Packet>{};
            input.serialize_forward(res);
            return res;
          }

          void encrypt(elle::cryptography::SecretKey const& k,
                       Packet const& p,
                       model::doughnut::Doughnut& dn)
          {
            elle::Buffer plain = packet::serialize(p, dn);
            payload = k.encipher(plain,
                                 elle::cryptography::Cipher::aes256,
                                 elle::cryptography::Mode::cbc,
                                 elle::cryptography::Oneway::sha256);
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
          serialize(elle::serialization::Serializer& s) override
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
          serialize(elle::serialization::Serializer& s) override
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
          Ping() = default;
          Ping(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("endpoint", remote_endpoint);
          }

          Endpoint remote_endpoint;
        };
        REGISTER(Ping, "ping");

        struct Pong: public Ping
        {
          using Super = Ping;
          using Super::Super;
        };
        REGISTER(Pong, "pong");

        struct BootstrapRequest: public Packet
        {
          BootstrapRequest() = default;
          BootstrapRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
          }
        };
        REGISTER(BootstrapRequest, "bootstrapRequest");

        struct FileBootstrapRequest: public Packet
        {
          FileBootstrapRequest() = default;
          FileBootstrapRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
          }
        };
        REGISTER(FileBootstrapRequest, "fileBootstrapRequest");

        static bool serialize_compress(elle::serialization::Serializer& s)
        {
          return s.context().has<CompressPeerLocations>();
        }

        struct Gossip: public Packet
        {
          Gossip() = default;
          Gossip(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            using Cfiles
              = std::unordered_multimap<Address, std::pair<Time, int>>;
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("contacts", contacts);
            if (!serialize_compress(s))
              s.serialize("files", files);
            else if (s.out())
            { // out
              std::vector<Address> addresses;
              std::unordered_map<Address, int> index;
              auto cfiles = Cfiles{};
              for (auto f: files)
              {
                auto addr = f.second.second;
                int idx;
                auto it = index.find(addr);
                if (it == index.end())
                {
                  addresses.push_back(addr);
                  index.emplace(addr, addresses.size()-1);
                  idx = addresses.size()-1;
                }
                else
                  idx = it->second;
                cfiles.emplace(f.first,
                               std::make_pair(f.second.first, idx));
              }
              s.serialize("file_addresses", addresses);
              s.serialize("file_files", cfiles);
            }
            else
            { // in
              auto& sin = static_cast<elle::serialization::SerializerIn&>(s);
              auto addresses = sin.deserialize<std::vector<Address>>("file_addresses");
              auto cfiles = sin.deserialize<Cfiles>("file_files");
              files.clear();
              for (auto c: cfiles)
                files.emplace(c.first,
                              std::make_pair(c.second.first,
                                             addresses.at(c.second.second)));
            }
          }
          // address -> (last_seen, val)
          std::unordered_map<Address, std::vector<TimedEndpoint>> contacts;
          std::unordered_multimap<Address, std::pair<Time, Address>> files;
        };
        REGISTER(Gossip, "gossip");

        static
        void
        result_out(elle::serialization::Serializer& s,
                   std::vector<NodeLocations> const& results)
        {
          NodeLocations locs;
          std::unordered_map<Address, int> loc_indexes;
          for (auto const& r: results)
          {
            for (auto const& loc: r)
            {
              auto it = loc_indexes.find(loc.id());
              if (it == loc_indexes.end())
              {
                locs.push_back(loc);
                loc_indexes.emplace(loc.id(), locs.size() - 1);
              }
            }
          }
          // index results (int list)
          std::map<std::vector<int>, int> res_indexes;
          std::vector<std::vector<int>> output;
          for (auto const& r: results)
          {
            std::vector<int> o;
            for (auto const& loc: r)
            {
              auto it = loc_indexes.find(loc.id());
              ELLE_ASSERT(it != loc_indexes.end());
              o.push_back(it->second);
            }
            std::sort(o.begin(), o.end());
            auto it = res_indexes.find(o);
            if (it == res_indexes.end())
            {
              output.push_back(o);
              res_indexes.emplace(o, output.size()-1);
            }
            else
            {
              output.push_back({it->second * (-1) -1 }); //careful, 0 == -0
            }
          }
          s.serialize("result_endpoints", locs);
          s.serialize("result_indexes", output);
        }

        static
        void
        result_in(elle::serialization::SerializerIn& s,
                  std::vector<NodeLocations>& results)
        {
          auto const locs = s.deserialize<NodeLocations>("result_endpoints");
          auto const input = s.deserialize<std::vector<std::vector<int>>>("result_indexes");
          results.clear();
          for (auto const& o: input)
          {
            if (o.size() == 1 && o.front() < 0)
              results.emplace_back(results.at((o.front()+1)*(-1)));
            else
              results.emplace_back(elle::make_vector(o,
                                                     [&locs](auto i)
                                                     {
                                                       return locs.at(i);
                                                     }));
          }
        }

        struct MultiGetFileRequest
          : public Packet
        {
          MultiGetFileRequest()
          {}

          MultiGetFileRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("id", request_id);
            s.serialize("origin", originAddress);
            s.serialize("endpoint", originEndpoints);
            s.serialize("address", fileAddresses);
            s.serialize("ttl", ttl);
            s.serialize("count", count);
            if (serialize_compress(s))
            {
              if (s.in())
                result_in(static_cast<elle::serialization::SerializerIn&>(s),
                          results);
              else
                result_out(s, results);
            }
            else
              s.serialize("result", results);
          }

          int request_id;
          Address originAddress;
          std::vector<Endpoint> originEndpoints;
          int ttl;
          int count;
          std::vector<Address> fileAddresses;
          std::vector<NodeLocations> results;
        };
        REGISTER(MultiGetFileRequest, "mget");

        struct MultiGetFileReply: public Packet
        {
          MultiGetFileReply()
          {}

          MultiGetFileReply(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }


          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("sender", sender);
            s.serialize("observer", observer);
            s.serialize("id", request_id);
            s.serialize("origin", origin);
            s.serialize("address", fileAddresses);
            if (serialize_compress(s))
            {
              if (s.in())
                result_in(static_cast<elle::serialization::SerializerIn&>(s),
                          results);
              else
                result_out(s, results);
            }
            else
              s.serialize("result", results);
            s.serialize("ttl", ttl);
          }

          int request_id;
          /// node who created the request
          Address origin;
          std::vector<Address> fileAddresses;
          int ttl;
          std::vector<NodeLocations> results;
        };
        REGISTER(MultiGetFileReply, "mgetReply");

        struct GetFileRequest: public Packet
        {
          GetFileRequest()
          {}

          GetFileRequest(elle::serialization::SerializerIn& input)
          {
            serialize(input);
          }

          void
          serialize(elle::serialization::Serializer& s) override
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
          std::vector<Endpoint> originEndpoints;
          /// file address requested
          Address fileAddress;
          int ttl;
          /// number of results we want
          int count;
          /// partial result
          NodeLocations result;
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
          serialize(elle::serialization::Serializer& s) override
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
          NodeLocations result;
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
          NodeLocations insert_result;
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
          serialize(elle::serialization::Serializer& s) override
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
          NodeLocations results;
          NodeLocations insert_results;
        };
        REGISTER(PutFileReply, "putReply");

#undef REGISTER
      }

      struct PendingRequest
      {
        NodeLocations result;
        NodeLocations insert_result;
        std::vector<NodeLocations> multi_result;
        elle::reactor::Barrier barrier;
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

      /* Pick one item at random that matches filter.
       * Fallback to unfiltered pick if no element matches.
       */
      template<typename C, typename F>
      typename C::iterator
      random_from(C& container, F filter, std::default_random_engine& gen)
      {
        if (container.empty())
          return container.end();
        int ncandidates = std::count_if(container.begin(), container.end(),
          [&filter](auto const& v) { return filter(v.second);});
        if (!ncandidates)
          return random_from(container, gen);
        std::uniform_int_distribution<> random(0, ncandidates-1);
        int v = random(gen);
        auto it = container.begin();
        while (!filter(it->second)) ++it;
        while (v--)
        {
          ++it;
          while (!filter(it->second))
            ++it;
        }
        return it;
      }

      static
      bool
      contact_without_timeouts(Contact const& c)
      {
        static bool disable = elle::os::inenv("INFINIT_KELIPS_NO_SNUB");
        if (disable)
          return true;
        else
          return c.ping_timeouts == 0;
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
          while (any_of_equal(res, src[v]));
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
                 std::shared_ptr<Local> local,
                 infinit::model::doughnut::Doughnut* doughnut)
        : Overlay(doughnut, local)
        , _config(config)
        , _next_id(1)
        , _port(0)
        , _observer(!local)
        , _dropped_puts(0)
        , _dropped_gets(0)
        , _failed_puts(0)
      {
        if (!doughnut->encrypt_options().encrypt_rpc)
        {
          _config.encrypt = false;
          _config.accept_plain = true;
        }
        bool v4 = elle::os::getenv("INFINIT_NO_IPV4", "").empty();
        bool v6 = elle::os::getenv("INFINIT_NO_IPV6", "").empty()
          && doughnut->version() >= elle::Version(0, 7, 0);
        this->_self = Address(this->doughnut()->id());
        if (!local)
          ELLE_LOG("Running in observer mode");
        start();
        if (auto l = local)
        {
          l->on_fetch().connect(
            [this]
            (Address a, std::unique_ptr<infinit::model::blocks::Block> & b)
            {
              this->fetch(a, b);
            });
          l->on_store().connect(
            [this](infinit::model::blocks::Block const& b)
            {
              this->store(b);
            });
          l->on_remove().connect(
            [this](Address a)
            {
              this->remove(a);
            });
          l->on_connect().connect(
            [this](RPCServer& rpcs)
            {
              rpcs.add(
                "kelips_fetch_state",
                [this] ()
                {
                  SerState res;
                  res.first.emplace(_self, to_endpoints(_local_endpoints));
                  for (auto const& contacts: this->_state.contacts)
                    for (auto const& c: contacts)
                      res.first.emplace(c.second.address,
                                        to_endpoints(c.second.endpoints));
                  for (auto const& f: this->_state.files)
                    res.second.emplace_back(f.second.address, f.second.home_node);
                  // OH THE UGLY HACK, we need a place to store our own address
                  res.second.push_back(std::make_pair(Address::null, _self));
                  return res;
                });
              rpcs.add(
                "kelips_fetch_state2",
                [this] ()
                {
                  SerState2 res;
                  std::unordered_map<Address, int> index;
                  res.first.emplace_back(this->_self, to_endpoints(_local_endpoints));
                  index[_self] = 0;
                  for (auto const& contacts: this->_state.contacts)
                    for (auto const& c: contacts)
                    {
                      index[c.second.address] = res.first.size();
                      res.first.emplace_back(c.second.address,
                                             to_endpoints(c.second.endpoints));
                    }
                  std::multimap<Address, Address> ofiles; // ordered fileId -> owner
                  for (auto const& f: this->_state.files)
                    ofiles.emplace(f.second.address, f.second.home_node);
                  Address prev = Address::null;
                  for (auto const& f: ofiles)
                  {
                    auto faddr = f.first;
                    auto fhome = f.second;
                    int p = 0;
                    while (p<32 && faddr.value()[p] == prev.value()[p])
                      ++p;
                    std::string daddr(faddr.value()+p, faddr.value()+32);
                    int idx = 0;
                    auto it = index.find(fhome);
                    if (it == index.end())
                    {
                      res.first.emplace_back(fhome, Endpoints());
                      index[fhome] = res.first.size()-1;
                      idx = res.first.size()-1;
                    }
                    else
                      idx = it->second;
                    res.second.push_back(std::make_pair(daddr, idx));
                    prev = faddr;
                  }
                  return res;
                });
            });
          this->_port = l->server_endpoint().port();
          {
            using Filter = elle::network::Interface::Filter;
            auto filter = Filter::only_up | Filter::no_loopback | Filter::no_autoip;
            for (auto const& itf: elle::network::Interface::get_map(filter))
            {
              auto add = [this](auto const& addrs){
                for (auto const& addr: addrs)
                {
                  this->_local_endpoints.emplace_back(Endpoint(
                    boost::asio::ip::address::from_string(addr),
                    this->_port), now());
                  ELLE_DEBUG("add local endpoint %s:%s", addr, this->_port);
                }
              };
              if (v4)
                add(itf.second.ipv4_address);
              if (v6)
                add(itf.second.ipv6_address);
            }
          }
          reload_state(*l);
          this->engage();
        }
      }

      int
      Node::group_of(Address const& a) const
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
        ELLE_TRACE_SCOPE("%s: destruct", this);
        this->doughnut()->dock().utp_server().socket()->unregister_reader("KELIPSGS");
        _emitter_thread.reset();
        _pinger_thread.reset();
        elle::reactor::wait(_in_use);
        this->_state.contacts.clear();
      }

      SerState
      Node::get_serstate(NodeLocation const& location)
      {
        ELLE_DEBUG_SCOPE("fetch state of %f", location);
        // FIXME: don't duplicate remote creation
        try
        {
          ELLE_DEBUG_SCOPE("establish UTP connection");
          auto conn = this->doughnut()->dock().connect(location, true);
          if (!conn->connected())
          {
            // connect() will silently drop everything if conn ends up
            // being a duplicate
            elle::reactor::Waiter waiter = elle::reactor::waiter(conn->on_connection());
            for (int i=0; i< 50; ++i)
            {
              if (elle::reactor::wait(waiter, 100_ms) || conn->disconnected())
                break;
            }
          }
          if (!conn->connected() && !conn->disconnected())
            throw elle::Error("connect timeout");
          ELLE_DEBUG("linking remote");
          std::shared_ptr<model::doughnut::Remote> remote;
          if (!conn->disconnected())
          {
            remote = this->doughnut()->dock().make_peer(conn);
            remote->connect(5_sec);
            ELLE_DEBUG("remote ready");
            this->_peer_cache.emplace(remote->id(), remote);
          }
          else
          {
            auto id = conn->location().id();
            auto it = this->_peer_cache.find(id);
            if (it == this->_peer_cache.end())
              elle::err("%s is duplicate but not found in cache", id);
            remote = std::dynamic_pointer_cast<model::doughnut::Remote>(it->second);
          }
          if (this->doughnut()->version() < elle::Version(0, 7, 0) || packet::disable_compression)
          {
            auto rpc = remote->make_rpc<SerState()>("kelips_fetch_state");
            return rpc();
          }
          else
          {
            auto rpc = remote->make_rpc<SerState2()>("kelips_fetch_state2");
            SerState2 state = rpc();
            SerState res;
            for (auto const& c: state.first)
              if (!c.second.empty())
                res.first.insert(c);
            Address prev = Address::null;
            for (auto const& f: state.second)
            {
              Address next = prev;
              ELLE_ASSERT(f.first.size() <= 32);
              memcpy(
                const_cast<unsigned char*>(next.value() + 32 - f.first.size()),
                f.first.data(), f.first.size());
              res.second.emplace_back(next, state.first.at(f.second).first);
              prev = next;
            }
            // UGLY HACK we must preserve
            ELLE_DEBUG("got %s contacts and %s files", res.first.size(), res.second.size());
            res.second.emplace_back(Address::null, state.first.front().first);
            return res;
          }
        }
        catch (elle::Error const& e)
        {
          elle::err("connection failed to %s: %s", location, e);
        }
      }

      void
      Node::onPacket(elle::ConstWeakBuffer nbuf, Endpoint source)
      {
        auto lock = this->_in_use.lock();
        ELLE_DUMP("Received %s bytes packet from %s", nbuf.size(), source);
        auto buf = elle::Buffer(nbuf.contents()+8, nbuf.size()-8);
        static auto async = elle::os::getenv("INFINIT_KELIPS_ASYNC", false);
        elle::reactor::run(async,
                           "process",
                           [=] { this->process(buf, source);});
      }

      void
      Node::bootstrap(bool use_contacts,
                      NodeLocations const& peers)
      {
        ELLE_TRACE_SCOPE("%s: bootstrap", this);
        auto scanned = std::unordered_set<Address>{};
        NodeLocations candidates;
        if (use_contacts)
          for (auto const& c: this->_state.contacts[_group])
            candidates.emplace_back(
              c.second.address,
              to_endpoints(c.second.endpoints));
        candidates.insert(candidates.end(), peers.begin(), peers.end());
        while (!candidates.empty())
        {
          NodeLocation location = candidates.back();
          candidates.pop_back();
          bool const unknown = location.id() == model::Address::null;
          if (!unknown && contains(scanned, location.id()))
            continue;
          ELLE_DEBUG_SCOPE("bootstrap %f", location);
          try
          {
            SerState res = get_serstate(location);
            // ugly hack, embeded self address
            auto peer_id = res.second.back().second;
            res.second.pop_back();
            if (location.id() != model::Address::null &&
                peer_id != location.id())
              ELLE_WARN("endpoints for peer %f reach peer %f instead",
                        location.id(), peer_id);
            scanned.emplace(peer_id);
            process_update(res);
            for (auto const& c: this->_state.contacts[_group])
              if (!contains(scanned, c.first))
                candidates.emplace_back(
                  c.first,
                  to_endpoints(c.second.endpoints));
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("error bootstraping Kelips node: %s", e);
          }
        }
        ELLE_TRACE("scanned %s nodes", scanned.size());
        // FIXME: weed out over-duplicated blocks
      }

      void
      Node::_discover(NodeLocations const& peers)
      {
        if (!this->_observer)
          this->bootstrap(false, peers);
        for (auto peer: peers)
          send_bootstrap(peer);
      }

      bool
      Node::_discovered(model::Address id)
      {
        int g = group_of(id);
        return this->_state.observers.find(id) != this->_state.observers.end()
         || this->_state.contacts[g].find(id) != this->_state.contacts[g].end();
      }

      void
      Node::send_bootstrap(NodeLocation const& l)
      {
        packet::BootstrapRequest req;
        req.sender = this->_self;
        if (l.id() != Address::null)
        {
          Contact& c = *get_or_make(l.id(), false, l.endpoints());
          ELLE_DEBUG_SCOPE("send bootstrap to %f at %s", l.id(), l.endpoints());
          if (!_config.encrypt || _config.accept_plain)
            send(req, c);
          else
          {
            packet::RequestKey req(make_key_request());
            send(req, c);
            _pending_bootstrap_address.push_back(l.id());
          }
        }
        else
        {
          ELLE_DEBUG_SCOPE("send bootstrap to %s", l.endpoints());
          if (!_config.encrypt || _config.accept_plain)
          {
            for (auto const& ep: l.endpoints())
              send(req, ep.udp(), Address::null);
          }
          else
          {
            packet::RequestKey req(make_key_request());
            for (auto const& ep: l.endpoints())
              send(req, ep.udp(), Address::null);
          }
          for (auto ep: l.endpoints())
          {
            if (ep.address().is_v6() && ep.address().to_v6().is_v4_mapped())
              ep = Endpoint(ep.address().to_v6().to_v4(), ep.port());
            this->_pending_bootstrap_endpoints.emplace(ep.udp());
          }
        }
      }

      void
      Node::engage()
      {
        ELLE_TRACE_SCOPE("%s: start serving", this);
        if (!_observer)
          this->bootstrap();
        this->doughnut()->dock().utp_server().socket()->register_reader(
          "KELIPSGS", [this](elle::ConstWeakBuffer nbuf, Endpoint source)
          {
            this->onPacket(nbuf, source);
          });
        ELLE_LOG("%s: listening on %s",
          this, this->doughnut()->dock().utp_server().local_endpoint());
        this->_pinger_thread.reset(
          new elle::reactor::Thread("pinger", [this]{  this->pinger(); }));
        if (!_observer)
          this->_emitter_thread.reset(
            new elle::reactor::Thread("emitter", [this]{ this->gossipEmitter(); }));
        ELLE_DEBUG("contact group nodes")
          for (auto& c: _state.contacts[_group])
            this->send_bootstrap(
              NodeLocation(c.second.address,
                           to_endpoints(c.second.endpoints)));
        if (this->_config.wait)
          wait(this->_config.wait);
      }

      void Node::start()
      {
        _group = group_of(_self);
        _state.contacts.resize(_config.k);
        // If we are not an observer, we must wait for Local port information
        if (_observer)
          engage();
      }

      void
      Node::setKey(Address const& a,
                   elle::cryptography::SecretKey sk,
                   bool observer)
      {
        auto oldkey = getKey(a);
        if (oldkey.first)
        {
          ELLE_DEBUG("%s: overriding key %s -> %s  for %s",
            *this, key_hash(*oldkey.first), key_hash(sk), a);
          _keys.find(a)->second = std::make_pair(std::move(sk), observer);
        }
        else
        {
          _keys.emplace(a, std::make_pair(std::move(sk), observer));
        }
      }

      std::pair<elle::cryptography::SecretKey*, bool>
      Node::getKey(Address const& a)
      {
        auto it = _keys.find(a);
        if (it != _keys.end())
          return std::make_pair(&it->second.first, it->second.second);
        return std::make_pair(nullptr, false);
      }

      void
      Node::send(packet::Packet& p, Contact& c)
      {
        ELLE_DUMP("send to contact %x", c.address);
        send(p, &c, nullptr, nullptr);
      }

      void
      Node::send(packet::Packet& p, Endpoint ep, Address addr)
      {
        ELLE_DUMP("send to endpoint %s : %s", addr, ep);
        send(p, nullptr, &ep, &addr);
      }

      void
      Node::send(packet::Packet& p, Contact* c, Endpoint* ep,
                 Address* addr)
      {
        //std::string ptype = elle::type_info(p).name();
        //std::cerr << ptype << std::endl;
        ELLE_ASSERT(c || ep);
        ELLE_ASSERT(c || addr);
        auto address = c ? c->address : *addr;
        p.observer = this->_observer;
        bool is_crypto = (dynamic_cast<const packet::EncryptedPayload*>(&p)
                          || dynamic_cast<const packet::RequestKey*>(&p)
                          || dynamic_cast<const packet::KeyReply*>(&p));
        elle::Buffer b;
        bool send_key_request = false;
        auto key = getKey(address);
        if (is_crypto
            || !_config.encrypt
            || (!key.first && _config.accept_plain)
          )
        {
          b = packet::serialize(p, *this->doughnut());
          send_key_request = _config.encrypt && !is_crypto;
        }
        else
        {
          if (!key.first)
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
              ep.encrypt(*key.first, p, *this->doughnut());
            }
            b = packet::serialize(ep, *this->doughnut());
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

        Endpoint e;
        if (ep)
          e = *ep;
        else if (c)
        {
          if (c->validated_endpoint)
            e = c->validated_endpoint->first;
          else
          {
            if (!c->contacter || c->contacter->done())
            {
              ELLE_DEBUG("Running contacter on %s", address);
              c->contacter.reset(
                new elle::reactor::Thread(
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
        elle::reactor::Lock l(_udp_send_mutex);
        static elle::Bench bench("kelips.send", 5_sec);
        elle::Bench::BenchScope bs(bench);
        ELLE_DUMP("%s: sending %s bytes packet to %s\n%x", *this, b.size(), e, b);
        b.size(b.size()+8);
        memmove(b.mutable_contents()+8, b.contents(), b.size()-8);
        memcpy(b.mutable_contents(), "KELIPSGS", 8);
        auto& sock = this->doughnut()->dock().utp_server().socket();
        static bool async = getenv("INFINIT_KELIPS_ASYNC_SEND");
        if (async)
        {
          auto sbuf = std::make_shared<elle::Buffer>(std::move(b));
          sock->socket()->async_send_to(
            boost::asio::buffer(sbuf->contents(), sbuf->size()),
            e.udp(),
            [sbuf] (  const boost::system::error_code& error,
              std::size_t bytes_transferred) {}
            );
        }
        else
        {
          try
          {
            sock->send_to(elle::ConstWeakBuffer(b), e.udp());
          }
          catch (elle::reactor::network::Error const&)
          { // FIXME: do something
          }
        }
      }

      void
      Node::process(elle::Buffer const& buf, Endpoint source)
      {
        //deserialize
        std::unique_ptr<packet::Packet> packet;
        elle::IOStream stream(buf.istreambuf());
        Serializer::SerializerIn input(stream, false);
        input.set_context(this->doughnut());
        if (this->doughnut()->version() >= elle::Version(0, 7, 0) && !packet::disable_compression)
          input.set_context(packet::CompressPeerLocations{});
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
          if (!key.first)
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
                plain = p->decrypt(*key.first, *this->doughnut());
              }
              if (plain->sender != p->sender)
              {
                ELLE_WARN("%s: sender inconsistency in encrypted packet: %s != %s",
                  *this, p->sender, plain->sender);
                return;
              }
              if (plain->observer < key.second)
              {
                ELLE_WARN("%s: sender %s is observer and sent a misflaged packet",
                          *this, p->sender);
                return;
              }
              if (key.second && (
                     dynamic_cast<packet::Gossip*>(plain.get())
                  || dynamic_cast<packet::GetFileReply*>(plain.get())
                  || dynamic_cast<packet::PutFileReply*>(plain.get())
                  ))
              {
                ELLE_WARN("%s: sender %s is observer and sent a non-observer packet",
                  *this, p->sender);
                return;
              }
              packet = std::move(plain);
              packet->endpoint = source;
              failure = false;
              was_crypted = true;
            }
            catch (elle::cryptography::Error const& e)
            {
              ELLE_DEBUG(
                "%s: decryption with %s from %s : %s failed: %s",
                *this, key_hash(*key.first), source, packet->sender, e.what());
            }
          }
          if (failure)
          { // send a key request
            ELLE_DEBUG("%s: sending key request to %s", *this, source);
            packet::RequestKey rk(make_key_request());
            send(rk, source, p->sender);
            return;
          }
        } // EncryptedPayload
        if (auto p = dynamic_cast<packet::RequestKey*>(packet.get()))
        {
          ELLE_DEBUG("%s: processing key request from %s", *this, source);
          // validate passport
          bool ok = doughnut()->verify(p->passport, false, false, false);
          if (!ok)
          {
            ELLE_WARN("%s: failed to validate passport from %s : %s",
              *this, source, p->sender);
            // send an error reply
            packet::KeyReply kr(doughnut()->passport());
            kr.sender = _self;
            kr.token = elle::Buffer("Failed to validate passport");
            send(kr, source, p->sender);
            return;
          }
          // sign the challenge with our passport
          auto signed_challenge = doughnut()->keys().k().sign(
            p->challenge,
            elle::cryptography::rsa::Padding::pss,
            elle::cryptography::Oneway::sha256);
          // generate key
          auto sk = elle::cryptography::secretkey::generate(256);
          elle::Buffer password = sk.password();
          bool observer = !p->passport.allow_storage();
          setKey(p->sender, std::move(sk), observer);
          packet::KeyReply kr(doughnut()->passport());
          kr.sender = _self;
          kr.encrypted_key = p->passport.user().seal(
            password,
            elle::cryptography::Cipher::aes256,
            elle::cryptography::Mode::cbc);
          kr.token = std::move(p->token);
          kr.signed_challenge = std::move(signed_challenge);
          ELLE_DEBUG("%s: sending keyreply to %s", *this, source);
          send(kr, source, p->sender);
          return;
        } // requestkey
        if (auto p = dynamic_cast<packet::KeyReply*>(packet.get()))
        {
          ELLE_DEBUG("%s: processing key reply from %s", *this, source);
          if (p->encrypted_key.empty())
          {
            ELLE_WARN("%s: key exchange failed with %s: %s", *this, source,
              p->token);
            return;
          }
          // validate passport
          bool ok = doughnut()->verify(p->passport, false, false, false);
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
            elle::cryptography::rsa::Padding::pss,
            elle::cryptography::Oneway::sha256);
          bool observer = !p->passport.allow_storage();
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
            elle::cryptography::Cipher::aes256,
            elle::cryptography::Mode::cbc);
          elle::cryptography::SecretKey sk(std::move(password));
          setKey(p->sender, std::move(sk), observer);
          // Flush operations waiting on crypto ready
          bool bootstrap_requested = false;
          {
            auto nsource = source;
            if (source.address().is_v6() && source.address().to_v6().is_v4_mapped())
              nsource = Endpoint{source.address().to_v6().to_v4(), source.port()};
            auto it = _pending_bootstrap_endpoints.find(nsource);
            if (it != _pending_bootstrap_endpoints.end())
            {
              ELLE_DEBUG("%s: processing queued operation to %s", *this, nsource);
              _pending_bootstrap_endpoints.erase(it);
              bootstrap_requested = true;
            }
          }
          {
            auto it = boost::range::find(_pending_bootstrap_address, p->sender);
            if (it != _pending_bootstrap_address.end())
            {
              *it = _pending_bootstrap_address[_pending_bootstrap_address.size() - 1];
              _pending_bootstrap_address.pop_back();
              bootstrap_requested = true;
            }
          }
          if (bootstrap_requested &&
              none_of_equal(_bootstrap_requests_sent, p->sender))
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
          r.observer = this->_observer;
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
        CASE(MultiGetFileRequest)
        onMultiGetFileRequest(p);
        CASE(MultiGetFileReply)
        onMultiGetFileReply(p);
        CASE(GetFileRequest)
        onGetFileRequest(p);
        CASE(GetFileReply)
        onGetFileReply(p);
        else
          ELLE_WARN("%s: Unknown packet type %s", *this, typeid(*p).name());
        #undef CASE
      };

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
          res.emplace(f, std::make_pair(fd.last_seen, fd.*access));
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
          res.emplace(f, fd.endpoints);
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
          res.emplace(f->address, f->endpoints);
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
        return boost::find_if(its,
          [&](auto const& e) { return e.second.second == v;}) != its.second;
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
            if (any_of_equal(indexes, v))
              --i;
            else
              indexes.push_back(v);
          }
          boost::sort(indexes);
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
                res.emplace(f.first,
                            std::make_pair(f.second.last_seen, f.second.home_node));
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
            if (any_of_equal(indexes, v))
              --i;
            else
              indexes.push_back(v);
          }
          boost::sort(indexes);
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
                res.emplace(f.first,
                            std::make_pair(f.second.last_seen, f.second.home_node));
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
          auto it = boost::find_if(its,
            [&](auto const& e) { return r.second.second == e.second.home_node;});
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
        elle::reactor::sleep(boost::posix_time::milliseconds(v));
        packet::Gossip p;
        p.sender = _self;
        p.observer = _observer;
        while (true)
        {
          elle::reactor::sleep(boost::posix_time::millisec(_config.gossip.interval_ms));
          p.contacts.clear();
          p.files.clear();
          p.contacts = pickContacts();
          elle::Buffer buf = serialize(p, *this->doughnut());
          auto targets = pickOutsideTargets();
          for (auto const& a: targets)
          {
            auto it = _state.contacts[group_of(a)].find(a);
            if (it != _state.contacts[group_of(a)].end())
              send(p, it->second);
          }
          // Add some files, just for group targets
          p.files = pickFiles();
          buf = serialize(p, *this->doughnut());
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
      Node::onContactSeen(Address addr, Endpoint endpoint, bool observer)
      {
        // Drop self
        if (addr == _self)
          return;
        int g = group_of(addr);
        Contact* c = get_or_make(addr, observer, {endpoint},
          observer || g == _group || signed(_state.contacts[g].size()) < _config.max_other_contacts);
        if (!c)
          return;
        // reset validated endpoint
        if (c->validated_endpoint && c->validated_endpoint->first != endpoint)
        {
          ELLE_LOG("%s: change validated endpoint %s -> %s", *this,
                   c->validated_endpoint->first, endpoint);
        }
        c->validated_endpoint = TimedEndpoint(endpoint, now());
        // add/update to endpoint list
        endpoints_update(c->endpoints, endpoint);
        c->ping_timeouts = 0;
      }

      void
      Node::onPong(packet::Pong* p)
      {
        auto tit = _ping_time.find(p->sender);
        if (tit == _ping_time.end())
        {
          ELLE_TRACE("%s: Unexpected or late pong from %f", *this, p->sender);
          return;
        }
        ELLE_DUMP("%s: got pong reply from %f", *this, p->sender);
        Duration d = now() - tit->second;
        _ping_time.erase(tit);
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
        Endpoint endpoint = p->remote_endpoint;
        endpoints_update(_local_endpoints, endpoint);
        auto contact_timeout = std::chrono::milliseconds(_config.contact_timeout_ms);
        endpoints_cleanup(_local_endpoints, now() - contact_timeout);
      }

      void
      Node::onGossip(packet::Gossip* p)
      {
        ELLE_DUMP("%s: processing gossip from %s", *this, p->endpoint);
        int g = group_of(p->sender);
        if (this->_observer)
        {
          ELLE_DEBUG("Observer got gossip from %s", p->sender);
          auto& cs = this->_state.contacts.at(g);
          auto it = cs.find(p->sender);
          Contact* c = nullptr;
          if (it == cs.end())
            c = this->get_or_make(p->sender, false, {p->endpoint}, true);
          else
            c = &it->second;
          if (!c->discovered)
          {
            c->discovered = true;
            this->on_discovery()(NodeLocation(p->sender, {p->endpoint}), false);
          }
        }
        if (g != _group && !p->files.empty())
          ELLE_WARN("%s: Received files from another group: %s at %s", *this, p->sender, p->endpoint);
        for (auto& c: p->contacts)
        {
          if (c.first == _self)
            continue;
          auto contact_timeout = std::chrono::milliseconds(_config.contact_timeout_ms);
          endpoints_cleanup(c.second, now() - contact_timeout);
          if (c.second.empty())
          {
            ELLE_DEBUG("%s: dropping contact entry %f with only obsolete endpoints",
                       *this, c.first);
            continue;
          }
          int g = group_of(c.first);
          auto& target = _state.contacts[g];
          auto it = target.find(c.first);
          if (it == target.end())
          {
            ELLE_LOG("%s: registering contact %f from gossip(%f)", *this, c.first, p->sender);
            if (g == _group || target.size() < (unsigned)_config.max_other_contacts)
            {
              target[c.first] = Contact{{}, c.second, c.first,
                                        Duration(), Time(), 0, {}, {}, true};
              this->on_discovery()(NodeLocation(c.first, to_endpoints(c.second)),
                                   false);
            }
          }
          else
            endpoints_update(it->second.endpoints, c.second);
        }
        if (g == _group)
        {
          for (auto const& f: p->files)
          {
            auto its = _state.files.equal_range(f.first);
            auto it = boost::find_if(its, [&](auto const& i) {
                return i.second.home_node == f.second.second;});
            if (it == its.second)
            {
              _state.files.emplace(f.first,
                                   File{f.first, f.second.second, f.second.first, Time(), 0});
              ELLE_DUMP("%s: registering %f live since %s (%s)", *this,
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
        ELLE_TRACE("%s: received bootstrap request from %f", this, p->sender);
        auto ep = p->endpoint;
        int g = group_of(p->sender);
        if (g == _group && !p->observer)
        {
          NodeLocation peer(p->sender, {ep});
          elle::reactor::Thread::unique_ptr t(
            new elle::reactor::Thread(
              elle::sprintf("rbootstrap(%f->%f)", this->id(), p->sender),
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
              }, false));
          auto ptr = t.get();
          _bootstraper_threads.emplace(ptr, std::move(t));
        }
        packet::Gossip res;
        res.sender = _self;
        int group_count = _state.contacts[g].size();
        // Special case to avoid the randomized fetcher running for ever
        if (group_count <= _config.gossip.bootstrap_group_target + 5)
        {
          for (auto const& e: _state.contacts[g])
            res.contacts.emplace(e.first, e.second.endpoints);
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
                res.contacts.emplace(e.first, e.second.endpoints);
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
      Node::addLocalResults(packet::MultiGetFileRequest* p,
        elle::reactor::yielder<std::pair<Address, NodeLocation>> const* yield,
        std::vector<std::set<Address>>& result_sets)
      {
        ELLE_ASSERT_LTE(p->results.size(), p->fileAddresses.size());
        p->results.resize(p->fileAddresses.size());
        for (unsigned i=0; i<p->fileAddresses.size(); ++i)
        {
          packet::GetFileRequest gfr;
          gfr.fileAddress = p->fileAddresses[i];
          gfr.result = p->results[i];
          std::function <void(NodeLocation)> yield_next = [&](NodeLocation pl)
          {
            if (yield)
              if (result_sets[i].insert(pl.id()).second)
                (*yield)(std::make_pair(gfr.fileAddress, pl));
          };
          addLocalResults(&gfr, &yield_next);
          p->results[i] = gfr.result;
        }
      }

      void
      Node::addLocalResults(packet::GetFileRequest* p,
                            elle::reactor::yielder<NodeLocation> const* yield)
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
          if (any_of(p->result,
                     [&](NodeLocation const& r) {
                       return r.id() == it->second.home_node;
                     }))
            continue;
          // find the corresponding endpoints
          auto endpoints = Endpoints{};
          bool found = false;
          if (it->second.home_node == _self)
          {
            ELLE_DEBUG("%s: found self", *this);
            if (_local_endpoints.empty())
            {
              ELLE_TRACE("Endpoint yet unknown, assuming localhost");
              endpoints.emplace(
                boost::asio::ip::address::from_string("127.0.0.1"),
                this->_port);
            }
            else
            {
              endpoints = to_endpoints(_local_endpoints);
            }
            found = true;
          }
          else
          {
            auto contact_it = _state.contacts[fg].find(it->second.home_node);
            if (contact_it != _state.contacts[fg].end())
            {
              endpoints = to_endpoints(contact_it->second.endpoints);
              ELLE_DEBUG("%s: found other at %f:%s",
                         *this, it->second.home_node, endpoints);
              found = true;
            }
            else
              ELLE_TRACE("%s: have file but not node", *this);
          }
          if (!found)
            continue;
          NodeLocation res(it->second.home_node, endpoints);
          p->result.push_back(res);
          if (yield)
            (*yield)(res);
        }
        nlocalhit.add(nhit);
      }

      void
      Node::onMultiGetFileRequest(packet::MultiGetFileRequest* p)
      {
        ELLE_TRACE("%s: getFileRequest %s/%x %s/%s", *this, p->request_id, p->fileAddresses,
          p->results.size(), p->count);
        if (p->fileAddresses.empty())
          return;
        if (p->originEndpoints.empty())
          p->originEndpoints = {p->endpoint};
        int fg = group_of(p->fileAddresses.front());
        if (fg == _group)
        {
          std::vector<std::set<Address>> result_sets;
          addLocalResults(p, nullptr, result_sets);
        }
        bool done = true;
        for (unsigned int i=0; i<p->fileAddresses.size(); ++i)
        {
          if (p->results[i].size() < unsigned(p->count))
          {
            done = false; break;
          }
        }
        auto const& fg_contacts = _state.contacts[fg];
        if (done
          || fg_contacts.empty()
          || (fg_contacts.size() == 1
            && fg_contacts.find(p->originAddress) != fg_contacts.end())
          || p->ttl == 0
          )
        {  // We got the full result or we cant forward, send reply
          packet::MultiGetFileReply res;
          res.sender = _self;
          res.fileAddresses = p->fileAddresses;
          res.origin = p->originAddress;
          res.request_id = p->request_id;
          res.results = p->results;
          res.ttl = p->ttl;
          if (p->originAddress == _self)
            onMultiGetFileReply(&res);
          else
          {
            Contact& c = *get_or_make(p->originAddress, true, p->originEndpoints);
            ELLE_TRACE("%s: replying to %s/%s", *this, p->originEndpoints, p->request_id);
            send(res, c);
          }
          return;
        }
        ELLE_TRACE("%s: route %s", *this, p->ttl);
        p->ttl--;
        p->sender = _self;
        auto it = random_from(_state.contacts[fg], contact_without_timeouts, _gen);
        if (it != _state.contacts[fg].end())
          send(*p, it->second);
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
            p->result.emplace_back(
              it->first,
              to_endpoints(it->second.endpoints));
        }
        else if (fg == _group)
        {
          addLocalResults(p, nullptr);
        }
        auto const& fg_contacts = _state.contacts[fg];
        if (p->result.size() >= unsigned(p->count)
          || fg_contacts.empty()
          || (fg_contacts.size() == 1
              && fg_contacts.find(p->originAddress) != fg_contacts.end())
            )
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
            Contact& c = *get_or_make(p->originAddress, true, p->originEndpoints);
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
            Contact& c = *get_or_make(p->originAddress, true, p->originEndpoints);
            send(res, c);
          }
          _dropped_gets++;
          return;
        }
        p->ttl--;
        p->sender = _self;
        auto it = random_from(_state.contacts[fg], contact_without_timeouts, _gen);
        if (it != _state.contacts[fg].end())
          send(*p, it->second);
      }

      void
      Node::onMultiGetFileReply(packet::MultiGetFileReply* p)
      {
        ELLE_DEBUG("%s: got reply for %x: %s", *this, p->fileAddresses, p->results);
        auto it = _pending_requests.find(p->request_id);
        if (it == _pending_requests.end())
        {
          ELLE_TRACE("%s: Unknown request id %s", *this, p->request_id);
          return;
        }
        ELLE_DEBUG("%s: unlocking waiter on response %s: %s", *this, p->request_id,
                   p->results);
        static elle::Bench stime = elle::Bench("kelips.GETM_RTT", boost::posix_time::seconds(5));
        stime.add(std::chrono::duration_cast<std::chrono::microseconds>(
          (now() - it->second->startTime)).count());
        static elle::Bench shops = elle::Bench("kelips.GETM_HOPS", boost::posix_time::seconds(5));
        shops.add(p->ttl);

        it->second->multi_result = p->results;
        it->second->barrier.open();
        _pending_requests.erase(it);
      }

      void
      Node::onGetFileReply(packet::GetFileReply* p)
      {
        ELLE_DEBUG("%s: got reply for GET %f from %f: %s", *this,
                   p->fileAddress, p->sender, p->result);
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

      static bool in_peerlocation(NodeLocations const& pl,
                                  Address addr)
      {
        return any_of(pl,
                      [&] (NodeLocation const& r) {
                        return r.id() == addr;
                      });
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
        bool quota_check = true;
        if (local()
          && local()->storage()
          && local()->storage()->capacity())
          quota_check = local()->storage()->capacity() > local()->storage()->usage() + 1024*1024 * 2;
        if (quota_check
          &&  fg == _group
          &&  ((p->insert_ttl == 0 && !_local_endpoints.empty())
              || _state.contacts[_group].empty()))
        {
          if (!in_peerlocation(p->result, _self)
            && !in_peerlocation(p->insert_result, _self))
          // check if we didn't already accept this file
          {
            // Check if we already have the block
            auto its = _state.files.equal_range(p->fileAddress);
            auto it = boost::find_if(its, [&](auto const& i) {
                return i.second.home_node == _self;
              });
            if (it == its.second)
            { // Nope, insert here
              // That makes us a home node for this address, but
              // wait until we get the RPC to store anything
              ELLE_DEBUG("%s: inserting in insert_result", *this);
              p->insert_result.emplace_back(
                this->_self, to_endpoints(_local_endpoints));
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
            Contact& c = *get_or_make(p->originAddress, true,
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
        auto it = random_from(_state.contacts[fg], contact_without_timeouts, _gen);
        if (it == _state.contacts[fg].end())
          it = random_from(_state.contacts[_group], contact_without_timeouts, _gen);
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
        ELLE_DEBUG("%s: got reply for PUT %f from %f: %s",
          *this, p->request_id, p->sender, p->results);
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
      Node::kelipsMGet(std::vector<Address> files, int n,
                       std::function<void (std::pair<Address, NodeLocation>)> yield)
      {
        BENCH("kelipsMGet");
        ELLE_TRACE_SCOPE("%s: mget %s", *this, files);
        std::vector<std::set<Address>> result_sets;
        result_sets.resize(files.size());
        packet::MultiGetFileRequest r;
        r.sender = _self;
        r.request_id = ++ _next_id;
        r.originAddress = _self;
        for (auto const& te: _local_endpoints)
          r.originEndpoints.push_back(te.first);
        r.fileAddresses = files;
        r.ttl = _config.query_get_ttl;
        r.count = n;
        int fg = group_of(files.front());
        if (fg == _group)
        {
          addLocalResults(&r, &yield, result_sets);
          bool done = true;
          for (unsigned int i=0; i<files.size(); ++i)
          {
            if (result_sets[i].size() < unsigned(n))
            {
              done = false; break;
            }
          }
          if (done)
            return;
        }
        for (int i = 0; i < _config.query_get_retries; ++i)
        {
          packet::MultiGetFileRequest req(r);
          req.request_id = ++this->_next_id;
          auto r = std::make_shared<PendingRequest>();
          r->startTime = now();
          r->barrier.close();
          // Select target node
          auto it = random_from(_state.contacts[fg], contact_without_timeouts, _gen);
          if (it == _state.contacts[fg].end())
            it = random_from(_state.contacts[_group], contact_without_timeouts, _gen);
          if (it == _state.contacts[_group].end())
          {
            ELLE_TRACE("no contact to forward GET to");
            continue;
          }
          auto ir =
            this->_pending_requests.emplace(req.request_id, r);
          ELLE_ASSERT(ir.second);
          ELLE_DEBUG("%s: get request %s(%s)", *this, i, req.request_id);
          send(req, it->second);
                      elle::reactor::wait(r->barrier,
              boost::posix_time::milliseconds(_config.query_timeout_ms));
          if (!r->barrier.opened())
          {
            ELLE_LOG("%s: mget request to %s on %s timeout (try %s)",
              *this, it->second, files, i);
            this->_pending_requests.erase(ir.first);
          }
          else
          {
            ELLE_TRACE("request %s (%s) gave %s results",
              i, req.request_id, r->multi_result.size());
            ELLE_DUMP("got %s", r->multi_result);
            for (unsigned f = 0; f < r->multi_result.size(); ++f)
            {
              for (auto fr: r->multi_result[f])
                if (result_sets[f].insert(fr.id()).second)
                  yield(std::make_pair(files[f], fr));
            }
            bool done = true;
            for (unsigned i=0; i<files.size(); ++i)
            {
              if (result_sets[i].size() < unsigned(n))
              {
                done = false; break;
              }
            }
            if (done)
              break;
          }
        }
      }

      void
      Node::kelipsGet(Address file, int n, bool local_override, int attempts,
                      bool query_node,
                      bool fast_mode,
                      std::function <void(NodeLocation)> yield,
                      bool ignore_local_cache)
      {
        BENCH("kelipsGet");
        ELLE_TRACE_SCOPE("%s: get %s", *this, file);
        if (attempts == -1)
          attempts = _config.query_get_retries;
        auto f = [this,file,n,local_override, attempts, yield, query_node, fast_mode, ignore_local_cache]() {
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
          if (!query_node && fg == _group && !ignore_local_cache)
          {
            // check if we have it locally
            auto its = _state.files.equal_range(file);
            auto it_us = boost::find_if(its,
              [&](std::pair<const infinit::model::Address, File> const& f) {
                return f.second.home_node == _self;
              });
            if (it_us != its.second && (n == 1 || local_override || fast_mode))
            {
              ELLE_DEBUG("get satifsfied locally");
              yield(NodeLocation(this->id(), {}));
              return;
            }
            // add result for our own file table
            addLocalResults(&r, &yield);
            for (auto const& e: r.result)
              result_set.insert(e.id());
            if (result_set.size() >= unsigned(fast_mode ? 1 : n))
            { // Request completed locally
              ELLE_DEBUG("Driver exiting");
              bench_localresult.add(1);
              return;
            }
          }
          if (query_node && !ignore_local_cache)
          {
            auto& target = _state.contacts[fg];
            auto it = target.find(file);
            if (it != target.end())
            {
              yield(
                NodeLocation(
                  it->first,
                  to_endpoints(it->second.endpoints)));
              return;
            }
          }
          ELLE_TRACE_SCOPE("%s: get did not complete locally (%s)",
                           this, result_set.size());
          for (int i = 0; i < attempts; ++i)
          {
            packet::GetFileRequest req(r);
            req.request_id = ++this->_next_id;
            auto r = std::make_shared<PendingRequest>();
            r->startTime = now();
            r->barrier.close();
            // Select target node
            auto it = random_from(_state.contacts[fg], contact_without_timeouts,
                                  _gen);
            if (it == _state.contacts[fg].end())
              it = random_from(_state.contacts[_group], contact_without_timeouts,
                               _gen);
            if (it == _state.contacts[_group].end())
            {
              ELLE_TRACE("no contact to forward GET to");
              break;
            }
            auto ir = this->_pending_requests.emplace(req.request_id, r);
            ELLE_ASSERT(ir.second);
            ELLE_DEBUG("%s: get request %s(%s)", *this, i, req.request_id);
            send(req, it->second);
            elle::reactor::wait(r->barrier,
              boost::posix_time::milliseconds(_config.query_timeout_ms));
            if (!r->barrier.opened())
            {
              ELLE_TRACE("%s: get request on %s timeout (try %s)",
                        *this, file, i);
              this->_pending_requests.erase(ir.first);
            }
            else
            {
              ELLE_DEBUG("request %s (%s) gave %s results",
                         i, req.request_id, r->result.size());
              for (auto const& e: r->result)
              {
                if (fg == _group && !query_node)
                { // oportunistically add the entry to our tables
                  auto its = _state.files.equal_range(file);
                  auto it_r = boost::find_if(its,
                    [&](std::pair<const infinit::model::Address, File> const& f) {
                      return f.second.home_node == e.id();
                    });
                  if (it_r == its.second)
                  _state.files.emplace(file, File{file, e.id(), now(), Time(), 0});
                }
                if (result_set.insert(e.id()).second)
                  yield(e);
              }
              if (signed(result_set.size()) >= (fast_mode ? 1 : n))
                break;
            }
          }
          if (result_set.empty() && ignore_local_cache)
          {
            ELLE_DEBUG("%s: result set empty, using local cache", this);
            if (query_node)
            {
              auto& target = _state.contacts[fg];
              auto it = target.find(file);
              if (it != target.end())
              {
                yield(
                  NodeLocation(
                    it->first,
                    to_endpoints(it->second.endpoints)));
              }
            }
          }
        };
        return f();
      }

      NodeLocations
      Node::kelipsPut(Address file, int n)
      {
        BENCH("kelipsPut");
        ELLE_TRACE_SCOPE("%s: put %s on %s nodes", this, file, n);
        int fg = group_of(file);
        packet::PutFileRequest p;
        p.query_node = false;
        p.sender = _self;
        p.originAddress = _self;
        p.observer = this->_observer;
        for (auto const& te: _local_endpoints)
          p.originEndpoints.push_back(te.first);
        p.fileAddress = file;
        p.ttl = _config.query_put_ttl;
        p.count = n;
        // If there is only two nodes, inserting is deterministic without the rand
        p.insert_ttl = _config.query_put_insert_ttl + (rand()%2);
        NodeLocations results;
        NodeLocations insert_results;
        for (int i = 0; i < _config.query_put_retries; ++i)
        {
          packet::PutFileRequest req = p;
          req.request_id = ++_next_id;
          auto r = std::make_shared<PendingRequest>();
          r->startTime = now();
          r->barrier.close();
          elle::Buffer buf = serialize(req, *this->doughnut());
          // Select target node
          auto it = random_from(_state.contacts[fg], contact_without_timeouts, _gen);
          if (it == _state.contacts[fg].end())
            it = random_from(_state.contacts[_group], contact_without_timeouts, _gen);
          if (it == _state.contacts[_group].end())
          {
            if (fg != this->_group || this->_observer)
            {
              ELLE_TRACE("no suitable node found");
              return {};
            }
            ELLE_TRACE("no peer, store locally");
            this->_promised_files.push_back(p.fileAddress);
            results.emplace_back(this->id(), model::Endpoints());
            return results;
          }
          _pending_requests[req.request_id] = r;
          ELLE_DEBUG("%s: put request %s(%s)", *this, i, req.request_id);
          send(req, it->second);
          elle::reactor::wait(r->barrier,
            boost::posix_time::milliseconds(_config.query_timeout_ms));
          if (!r->barrier.opened())
          {
            ELLE_LOG("%s: Timeout on PUT attempt %s", *this, i);
            _pending_requests.erase(req.request_id);
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
          results.resize(n, NodeLocation(Address::null, {}));
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
        using US = std::chrono::duration<int, std::ratio<1, 1000000>>;
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
        elle::reactor::sleep(boost::posix_time::milliseconds(v));
        int counter = 0;
        while (true)
        {
          ELLE_DUMP("%s: sleep for %s ms", *this, _config.ping_interval_ms);
          elle::reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
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
              elle::reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
              continue;
            }
            std::uniform_int_distribution<> random2(0, _state.contacts[group].size()-1);
            int v = random2(_gen);
            auto it = _state.contacts[group].begin();
            while(v--) ++it;
            auto tit = _ping_time.find(it->second.address);
            if (tit != _ping_time.end())
            {
              if (now() - tit->second < std::chrono::milliseconds(_config.ping_timeout_ms))
              {
                elle::reactor::sleep(boost::posix_time::milliseconds(_config.ping_interval_ms));
                continue;
              }
              else
              {
                it->second.ping_timeouts++;
                ELLE_TRACE("%s: ping timeout on %s (%s)", this, it->first,
                  it->second.ping_timeouts);
              }
            }
            target = &it->second;
            break;
          }
          packet::Ping p;
          p.sender = _self;
          p.observer = this->_observer;
          ELLE_DUMP("%s: pinging %x", *this, target->address);
          _ping_time[target->address] = now();
          send(p, *target);
          ++counter;
          if (this->_observer && ! (counter % 50))
          {
            // observers get notified on new peers upon discovery by
            // non-observer nodes. But it might fail if the packet is lost,
            // or if the observer was dropped from the other's contact tables.
            // so, periodically make a BootstrapRequest to get contacts.
            packet::BootstrapRequest p;
            p.sender = _self;
            p.observer = true;
            send(p, *target);
          }
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
        int idx = 0;
        for (auto& contacts: _state.contacts)
        {
          auto it = contacts.begin();
          while (it != contacts.end())
          {
            endpoints_cleanup(it->second.endpoints, now() - contact_timeout);
            if (it->second.endpoints.empty())
            {
              ELLE_LOG("%s: erase %s from %s", *this, it->second, idx);
              auto addr = it->second.address;
              bool discovered = it->second.discovered;
              it = contacts.erase(it);
              if (discovered)
                this->on_disappearance()(addr, false);
            }
            else
              ++it;
          }
          ++idx;
        }
        // check observers too
        {
          auto it = this->_state.observers.begin();
          while (it != this->_state.observers.end())
          {
            endpoints_cleanup(it->second.endpoints, now() - contact_timeout);
            if (it->second.endpoints.empty())
            {
              ELLE_LOG("%s: erase %s from observers", *this, it->second);
              it = this->_state.observers.erase(it);
            }
            else
              ++it;
          }
        }
        // Check ping timeouts
        auto today = now();
        for (auto it = this->_ping_time.begin(); it != this->_ping_time.end();)
        {
          if (today - it->second > std::chrono::milliseconds(_config.ping_timeout_ms))
          {
            auto g = this->group_of(it->first);
            auto cit = this->_state.contacts[g].find(it->first);
            if (cit != this->_state.contacts[g].end())
            {
              cit->second.ping_timeouts++;
            }
            it = this->_ping_time.erase(it);
          }
          else
            ++it;
        }
        int time_send_all
          = _state.files.size() / (_config.gossip.files/2 ) *  _config.gossip.interval_ms;
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
      Node::store(infinit::model::blocks::Block const& block)
      {
        if (none_of(elle::as_range(_state.files.equal_range(block.address())),
                    [&](Files::value_type const& f) {
                      return f.second.home_node == _self;
                    }))
          _state.files.emplace(block.address(),
                               File{block.address(), _self, now(), Time(), 0});
        auto itp = boost::range::find(_promised_files, block.address());
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

      Overlay::WeakMember
      Node::make_peer(NodeLocation hosts) const
      {
        auto it = this->_peer_cache.find(hosts.id());
        if (it != this->_peer_cache.end())
        {
          auto* remote = dynamic_cast<model::doughnut::Remote*>(it->second.get());
          if (!remote || !remote->connection()->disconnected())
          {
            // check if we have new endpoints available
            bool new_endpoints = false;
            if (remote)
            {
              auto const& eps = remote->endpoints();
              for (auto const& ep: hosts.endpoints())
                if (none_of_equal(eps, ep))
                {
                  new_endpoints = true;
                  break;
                }
            }
            if (!new_endpoints)
            {
              ELLE_DEBUG("%s: returning existing remote for %s", this, hosts);
              return Overlay::WeakMember::own(it->second);
            }
          }
          elle::With<elle::reactor::Thread::NonInterruptible>() << [&](elle::reactor::Thread::NonInterruptible&) {
            elle::unconst(this->_peer_cache).erase(it);
          };
        }
        ELLE_DEBUG("%s: querying new remote for %s", this, hosts);
        auto w = this->doughnut()->dock().make_peer(hosts);
        elle::unconst(this->_peer_cache).emplace(hosts.id(), w.lock());
        return Overlay::WeakMember::own(w.lock());
      }

      auto
      Node::_lookup(std::vector<infinit::model::Address> const& addresses,
                    int n) const
        -> LocationGenerator
      {
        if (this->doughnut()->version() < elle::Version(0, 6, 0))
          return Overlay::_lookup(addresses, n);
        auto grouped = std::vector<std::vector<model::Address>>{};
        for (auto a: addresses)
        {
          int g = group_of(a);
          if (grouped.size() <= unsigned(g))
            grouped.resize(g+1);
          grouped[g].push_back(a);
        }
        return [this, n, grouped](LocationGenerator::yielder const& yield)
        {
          for (auto g: grouped)
            if (!g.empty())
            {
              elle::unconst(this)->kelipsMGet(g, n,
                                              [&](std::pair<Address, NodeLocation> ap)
              {
                yield(std::make_pair(ap.first,
                                     elle::unconst(this)->make_peer(ap.second)));
              });
            }
        };
      }


      auto
      Node::_allocate(infinit::model::Address address, int n) const
        -> MemberGenerator
      {
        BENCH("allocate");
        return [this, address, n](MemberGenerator::yielder const& yield)
          {
            for (auto r: elle::unconst(this)->kelipsPut(address, n))
              yield(elle::unconst(this)->make_peer(r));
          };
      }

      auto
      Node::_lookup(infinit::model::Address address,
                    int n, bool fast) const
        -> MemberGenerator
      {
        BENCH("lookup");
        return [this, address, n, fast](MemberGenerator::yielder const& yield)
          {
            std::function<void(NodeLocation)> handle = [&](NodeLocation hosts)
              {
                yield(elle::unconst(this)->make_peer(hosts));
              };
            elle::unconst(this)->kelipsGet(
              address, n, false, -1, false, fast, handle);
          };
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
          elle::reactor::sleep(1_sec);
        }
        elle::reactor::sleep(1_sec);
      }

      void
      Node::reload_state(Local& l)
      {
        auto keys = l.storage()->list();
        for (auto const& k: keys)
        {
          _state.files.emplace(k,
            File{k, _self, now(), now(), _config.gossip.new_threshold + 1});
          //ELLE_DUMP("%s: reloaded %x", *this, k);
        }
      }

      void
      Node::process_update(SerState const& s)
      {
        auto notify_observers = [this](NodeLocation const& nl)
        {
          packet::Gossip p;
          p.sender = this->id();
          p.observer = false;
          auto& c = p.contacts[nl.id()];
          for (auto const& e: nl.endpoints())
            c.emplace_back(e, now());
          for (auto& obs: this->_state.observers)
            this->send(p, obs.second);
        };
        if (this->_observer)
          ELLE_WARN("Unexpected update received from observer");
        ELLE_DEBUG("register %s contacts and %s blocks",
                   s.first.size(), s.second.size());
        for (auto const& c: s.first)
        {
          if (c.first == _self)
            continue;
          int g = group_of(c.first);
          Contacts& target = _state.contacts.at(g);
          auto it = target.find(c.first);
          if (it == target.end())
          {
            if (g == this->_group ||
                signed(target.size()) < this->_config.max_other_contacts)
            {
              auto contact =
                Contact{{}, {}, c.first, Duration(0), Time(), 0, {}, {}, true};
              for (auto const& ep: c.second)
                contact.endpoints.push_back(TimedEndpoint(ep, now()));
              auto nl = NodeLocation(c.first, c.second);
              ELLE_LOG("%s: register %f", this, contact);
              target[c.first] = std::move(contact);
              this->on_discovery()(nl, false);
              notify_observers(nl);
            }
          }
          else
          {
            for (auto const& ep: c.second)
              endpoints_update(it->second.endpoints, ep);
            if (!it->second.discovered)
            {
              it->second.discovered = true;
              auto nl =
                NodeLocation(it->first, to_endpoints(it->second.endpoints));
              this->on_discovery()(nl, false);
              notify_observers(nl);
            }
          }
        }
        for (auto const& f: s.second)
        {
          if (group_of(f.first) != _group)
            continue;
          if (f.second == _self)
            continue;
          auto its = _state.files.equal_range(f.first);
          auto it = boost::find_if(its, [&](auto const& i) {
              return i.second.home_node == f.second;});
          if (it == its.second)
            _state.files.emplace(f.first,
                                 File{f.first, f.second, now(), now(),
                                     this->_config.gossip.new_threshold + 1});
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
        auto peers = to_endpoints(it->second.endpoints);
        // this yields, thus invalidating the iterator
        ELLE_TRACE("contacting %s on %s", id, peers);
        auto& rsock = this->doughnut()->dock().utp_server().socket();
        auto res = rsock->contact(id, Endpoints(peers).udp());
        ELLE_TRACE("contact %s yielded %s", id, res);
        it = contacts->find(address);
        if (it == contacts->end())
        {
          ELLE_TRACE("contact to removed entry %s, dropping", id)
          return;
        }
        if (!it->second.validated_endpoint)
        {
          it->second.validated_endpoint = TimedEndpoint(res, now());
        }
        else
          res = it->second.validated_endpoint->first.udp();
        std::vector<elle::Buffer> buf;
        std::swap(it->second.pending, buf);
        ELLE_DEBUG("flushing %s buffer(s) to %s on %s", buf.size(), id, res);
        for (auto& b: buf)
        {
          b.size(b.size()+8);
          memmove(b.mutable_contents()+8, b.contents(), b.size()-8);
          memcpy(b.mutable_contents(), "KELIPSGS", 8);
          try
          {
            rsock->send_to(elle::ConstWeakBuffer(b), res);
          }
          catch (elle::reactor::network::Error const& e)
          { // FIXME: do something
            ELLE_TRACE("network exception sending to %s: %s", res, e);
          }
        }
        if (!it->second.validated_endpoint && !it->second.pending.empty())
        {
          // validated_endpoint was reset, and new pending packets were added
          // since we were still running, no contacter was restarded, so
          // we need to do that now
          ELLE_WARN("validated_endpoint was reset for %s", res);
          contact(address);
        }
      }

      Contact*
      Node::get_or_make(Address address, bool observer,
                        Endpoints const& endpoints,
                        bool make)
      {
        Contacts* target = observer ?
          &_state.observers : &_state.contacts[group_of(address)];
        // Check the other map for misplaced entries. This can happen when
        // invoked with a packet's originAddress, for which we don't know the
        // observer status.
        if (target->find(address) == target->end())
        {
          Contacts* ntarget = !observer ?
            &_state.observers : &_state.contacts[group_of(address)];
          auto it = ntarget->find(address);
          if (it != ntarget->end())
          {
            if (!observer)
            {
              // Change
              ELLE_TRACE("moving misplaced entry for %x to %s", address,
                target == &_state.observers ? "observers" : "storage nodes");
              target->insert(std::make_pair(address, std::move(it->second)));
              ntarget->erase(address);
            }
            else
              target = ntarget;
          }
        }
        if (!make)
          return nullptr;
        auto c = Contact{{},  {}, address, Duration(), Time(), 0, {}, {}, observer};
        for (auto const& ep: endpoints)
          c.endpoints.push_back(TimedEndpoint(ep, now()));
        auto nl = NodeLocation(address, endpoints);
        auto inserted = target->insert(std::make_pair(address, std::move(c)));
        // for non-observers, only notify discovery after bootstrap completes
        if (inserted.second && observer)
          this->on_discovery()(nl, observer);
        if (!inserted.second)
        { // we still want the new endpoints
          auto sz = inserted.first->second.endpoints.size();
          for (auto const& ep: endpoints)
            endpoints_update(inserted.first->second.endpoints, ep);
          // Reset validated endpoint so that contacter is re-run
          if (sz != inserted.first->second.endpoints.size())
          {
            inserted.first->second.validated_endpoint.reset();
            // nodes will be notified in due time when the peer reconnects
            // to them, but observers wont
            packet::Gossip p;
            p.sender = this->id();
            p.observer = false;
            p.contacts[nl.id()] = inserted.first->second.endpoints;
            for (auto& obs: this->_state.observers)
              this->send(p, obs.second);
          }
        }
        return &inserted.first->second;
      }

      Overlay::WeakMember
      Node::_lookup_node(Address address) const
      {
        BENCH("lookup_node");
        if (address == _self)
          return this->local();
        auto async_lookup = [this, address]() {
          boost::optional<NodeLocation> result;
          elle::unconst(this)->kelipsGet(
            address, 1, false, -1, true, false,
            [&] (NodeLocation p)
            {
              result = p;
            });
          auto it = _node_lookups.find(address);
          if (it != _node_lookups.end())
            it->second.second = !!result;
        };
        auto it = _node_lookups.find(address);
        if (it != _node_lookups.end())
        {
          if (it->second.second == true)
          { // async lookup suceeded
            _node_lookups.erase(it);
          }
          else if (!it->second.first || it->second.first->done())
          { // restart async lookup
            it->second.first.reset(new elle::reactor::Thread("async_lookup",
              async_lookup));
            // and fast fail
            elle::err("Node %s not found", address);
          }
          else // thread still running
            elle::err("Node %s not found", address);
        }
        boost::optional<NodeLocation> result;
        elle::unconst(this)->kelipsGet(
          address, 1, false, -1, true, false, [&](NodeLocation p)
          {
            result = p;
          });
        if (!result)
        { // mark for future fast fail
          _node_lookups.emplace(address, std::make_pair(
            elle::reactor::Thread::unique_ptr(), false));
          elle::err("Node %s not found", address);
        }
        return make_peer(*result);
      }

      packet::RequestKey
      Node::make_key_request()
      {
        auto req = packet::RequestKey(doughnut()->passport());
        req.sender = _self;
        req.token = elle::cryptography::random::generate<elle::Buffer>(128);
        req.challenge = elle::cryptography::random::generate<elle::Buffer>(128);
        _challenges.emplace(req.token.string(), req.challenge);
        ELLE_DEBUG("Storing challenge %x", req.token);
        return req;
      }

      elle::json::Json
      Node::query(std::string const& k,
                  boost::optional<std::string> const& v)
      {
        elle::json::Object res;
        if (k == "protocol")
        {
          if (v)
            _config.rpc_protocol = model::doughnut::make_protocol(*v);
          else
            res["protocol"] = elle::sprintf("%s", _config.rpc_protocol);
        }
        else if (k == "stats")
        {
          res["group"] = this->_group;
          res["contacts"] = this->peer_list();
          res["files"] = this->_state.files.size();
          res["dropped_puts"] = this->_dropped_puts;
          res["dropped_gets"] = this->_dropped_gets;
          res["failed_puts"] = this->_failed_puts;
          elle::json::Array rtts;
          for (auto const& c: _state.contacts[_group])
          rtts.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(c.second.rtt).count());
          res["ping_rtt"] = rtts;
          elle::json::Array observers;
          for (auto& contact: this->_state.observers)
          {
            auto last_seen = std::chrono::duration_cast<std::chrono::seconds>
              (std::chrono::system_clock::now() -
               endpoints_max(contact.second.endpoints));
            elle::json::Array endpoints;
            for (auto const pair: contact.second.endpoints)
              endpoints.push_back(PrettyEndpoint(pair.first).repr());
            elle::json::Object obs {
              { "id", elle::sprintf("%x", contact.second.address) },
              { "validated_endpoint",
                elle::sprintf("%s", (contact.second.validated_endpoint
                  ? PrettyEndpoint(contact.second.validated_endpoint->first)
                  : PrettyEndpoint(Endpoint())).repr()) },
              { "endpoints", endpoints },
              { "last_seen", elle::sprintf("%ss", last_seen.count()) },
            };
            observers.push_back(obs);
          }
          res["observers"] = observers;
        }
        else if (k == "blockcount")
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
        else if (starts_with(k, "cachecheck."))
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
            this->_pending_requests.emplace(gf.request_id, r);
            send(gf, c.second);
            if (!elle::reactor::wait(r->barrier, 100_ms))
              ++hits[0];
            else
            {
              int nh = r->result.size();
              if (signed(hits.size()) <= nh)
                hits.resize(nh+1);
              hits[nh]++;
            }
          }
          res["counts"] = elle::make_vector(hits);
        }
        else if (starts_with(k, "scan."))
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
            auto count = std::distance(its.first, its.second);
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
              NodeLocations res;
              kelipsGet(addr, factor, false, 3, false, false, [&](NodeLocation pl) {
                  res.push_back(pl);
              });
              if (counts.size() <= res.size())
                counts.resize(res.size()+1, 0);
              counts[res.size()]++;
            }
          };
          elle::With<elle::reactor::Scope>() <<  [&] (elle::reactor::Scope& s)
          {
            for (int i=0; i<10; ++i)
              s.run_background("scanner", scanner);
            while (!elle::reactor::wait(s, 10_sec))
              ELLE_TRACE("scanner: %s remaining", to_scan.size());
          };
          elle::json::Array ares;
          for (auto c: counts)
            ares.push_back(c);
          res["counts"] = ares;
        }
        else if (k == "bootstrap")
        {
          bootstrap(false);
        }
        else if (k == "gossip")
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
        else if (starts_with(k, "node."))
        {
          Address target = Address::from_string(k.substr(5));
          Overlay::WeakMember n;
          try {
            n = this->lookup_node(target);
            res["status"] = "got it";
          }
          catch (elle::Error const& e)
          {
            res["status"] = std::string("failed: ") + e.what();
          }
        }
        return res;
      }

      /*-----------.
      | Monitoring |
      `-----------*/

      std::string
      Node::type_name() const
      {
        return "kelips";
      }

      elle::json::Array
      Node::peer_list() const
      {
        auto res = elle::json::Array{};
        for (int i = 0; i < signed(this->_state.contacts.size()); ++i)
        {
          auto const& group = this->_state.contacts[i];
          for (auto const& contact: group)
          {
            auto last_seen = std::chrono::duration_cast<std::chrono::seconds>
              (std::chrono::system_clock::now() -
               endpoints_max(contact.second.endpoints));
            auto endpoints
              = elle::make_vector(contact.second.endpoints,
                                  [](auto const pair)
                                  {
                                    return PrettyEndpoint(pair.first).repr();
                                  });
            res.emplace_back(elle::json::Object{
              { "id", elle::sprintf("%x", contact.second.address) },
              { "validated_endpoint",
                elle::sprintf("%s", (contact.second.validated_endpoint
                  ? PrettyEndpoint(contact.second.validated_endpoint->first)
                  : PrettyEndpoint(Endpoint())).repr()) },
              { "endpoints", endpoints },
              { "last_seen", elle::sprintf("%ss", last_seen.count()) },
              { "discovered", contact.second.discovered },
              { "group", i },
              { "ping_timeouts", contact.second.ping_timeouts},
            });
          }
        }
        return res;
      }

      elle::json::Object
      Node::stats() const
      {
        return
          {
            {"type", this->type_name()},
            {"protocol", elle::sprintf("%s", this->_config.rpc_protocol)},
            {"group", this->_group},
            {"statistics", elle::json::Object{
                { "files", this->_state.files.size() },
                { "dropped_puts", this->_dropped_puts },
                { "dropped_gets", this->_dropped_gets },
                { "failed_puts", this->_failed_puts },
              }
            },
        };
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
        , wait(0)
        , encrypt(false)
        , accept_plain(true)
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
        {
          // Backward
          std::vector<Endpoints> bootstrap_nodes;
          s.serialize("bootstrap_nodes", bootstrap_nodes);
        }
        s.serialize("wait", wait);
        s.serialize("encrypt", encrypt);
        s.serialize("accept_plain", accept_plain);
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
      Configuration::make(std::shared_ptr<model::doughnut::Local> local,
                          model::doughnut::Doughnut* dht)
      {
        return std::make_unique<Node>(*this, std::move(local), dht);
      }

      static const
      elle::serialization::Hierarchy<infinit::overlay::Configuration>::
      Register<Configuration> _registerKelipsOverlayConfig("kelips");
    }
  }
}
