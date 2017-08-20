#include <elle/test.hh>

#include <elle/err.hh>
#include <elle/Option.hh>

#include <elle/das/Symbol.hh>

#include <boost/optional.hpp>

#include <memo/grpc/memo_vs.grpc.pb.h>
#include <memo/model/MissingBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/doughnut/UB.hh>
#include <memo/overlay/kelips/Kelips.hh>
#include <memo/overlay/kouncil/Kouncil.hh>
#include <memo/silo/MissingKey.hh>

#include <tests/grpc.grpc.pb.h>

#include <memo/grpc/grpc.hh>
#include <memo/grpc/serializer.hh>

#include <grpc++/grpc++.h>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test.grpc");


inline
std::string
base_name(std::string const& s)
{
  auto p = s.find_last_of(":");
  if (p == s.npos)
    return s;
  else
    return s.substr(p+1);
}

class Protogen
{
public:
  Protogen() {}
  template<typename T>
  void
  protogen();
  std::unordered_map<std::string, std::string> messages;
};

template<typename T>
struct ProtoTypeName
{
  static std::string value() { return base_name(elle::type_info<T>().name());}
  static void recurse(Protogen& pg) { pg.protogen<T>();}
};

template<typename T>
struct ProtoTypeName<std::vector<T>>
{
  static std::string value() { return "repeated " + ProtoTypeName<T>::value();}
  static void recurse(Protogen& pg) { ProtoTypeName<T>::recurse(pg);}
};

template<typename...T>
struct ProtoTypeName<elle::Option<T...>>
{
  static std::string value() { return "NOTIMPLEMENTED";}
  static void recurse(Protogen& pg) {}
};

#define PROTO_TYPE(ctype, ptype)               \
template<>                                     \
struct ProtoTypeName<ctype>                    \
{                                              \
  static std::string value() { return ptype;}  \
  static void recurse(Protogen& pg) {}         \
}

// FIXME maybe optimize by using 'sint*' protobuf types
PROTO_TYPE(std::string, "string");
PROTO_TYPE(elle::Buffer, "string");
PROTO_TYPE(bool, "bool");
PROTO_TYPE(int64_t, "int64");
PROTO_TYPE(uint64_t, "uint64");
PROTO_TYPE(int32_t, "int64");
PROTO_TYPE(uint32_t, "uint64");
PROTO_TYPE(int16_t, "int32");
PROTO_TYPE(uint16_t, "uint32");
PROTO_TYPE(int8_t, "int32");
PROTO_TYPE(uint8_t, "uint32");
PROTO_TYPE(double, "double");
PROTO_TYPE(float, "double");

template<typename T>
struct ProtoTypeName<boost::optional<T>>
{
  static std::string value()
  {
    return ProtoTypeName<T>::value();
  }
  static void recurse(Protogen& pg) {}
};

template <typename O, typename M = typename elle::das::DefaultModel<O>::type>
struct ProtoHelper
{
  template <typename T>
  struct ProtoProcess
  {
    using type = int;
    static
    int
    value(Protogen& pg, int& fid, std::string& msg)
    {
      using type = typename M::template FieldType<T>::type;
      auto cls = ProtoTypeName<type>::value();
      msg += "  " + cls + " " + T::name() + " = " + std::to_string(++fid) + ";\n";
      ProtoTypeName<type>::recurse(pg);
      return 0;
    }
  };
  static
  void
  protogen(Protogen& pg)
  {
    auto tn = elle::type_info<O>().name();
    pg.messages[tn]; // create it
    int fid = 0;
    std::string msg = "message " + base_name(tn) + " {\n";
    M::Fields::template map<ProtoProcess>::value(pg, fid, msg);
    msg += "}\n";
    pg.messages[tn] = msg;
  }
};

template<typename T>
void
Protogen::protogen()
{
   ProtoHelper<T>::protogen(*this);
}


namespace grpc
{
  std::ostream& operator << (std::ostream& o, Status const& s)
  {
    return o << s.error_code() << ": " << s.error_message();
  }

  bool operator == (Status const& a, Status const& b)
  {
    return a.error_code() == b.error_code();
  }

  bool operator == (Status const& a, StatusCode const& b)
  {
    return a.error_code() == b;
  }
}

class DHTs
{
public:
  template <typename ... Args>
  DHTs(int count)
   : DHTs(count, {})
  {}

  template <typename ... Args>
  DHTs(int count,
       boost::optional<elle::cryptography::rsa::KeyPair> kp,
       Args ... args)
    : owner_keys(kp ? *kp : elle::cryptography::rsa::keypair::generate(512))
    , dhts()
  {
    pax = true;
    if (count < 0)
    {
      pax = false;
      count *= -1;
    }
    for (int i = 0; i < count; ++i)
    {
      this->dhts.emplace_back(paxos = pax,
                              owner = this->owner_keys,
                              std::forward<Args>(args) ...);
      for (int j = 0; j < i; ++j)
        this->dhts[j].overlay->connect(*this->dhts[i].overlay);
    }
  }

  struct Client
  {
    template<typename... Args>
    Client(std::string const& name, DHT dht, Args...args)
      : dht(std::move(dht))
    {}

    DHT dht;
  };

  template<typename... Args>
  DHT
  dht(bool new_key,
      boost::optional<elle::cryptography::rsa::KeyPair> kp,
      Args... args)
  {
    auto k = kp ? *kp
      : new_key ? elle::cryptography::rsa::keypair::generate(512)
      : this->owner_keys;
    ELLE_LOG("new client with owner=%f key=%f", this->owner_keys.K(), k.K());
    DHT client(owner = this->owner_keys,
               keys = k,
               storage = nullptr,
               dht::consensus_builder = no_cheat_consensus(pax),
               paxos = pax,
               std::forward<Args>(args) ...
               );
    for (auto& dht: this->dhts)
      dht.overlay->connect(*client.overlay);
    return client;
  }

  template<typename... Args>
  Client
  client(bool new_key,
         boost::optional<elle::cryptography::rsa::KeyPair> kp,
         Args... args)
  {
    DHT client = dht(new_key, kp, std::forward<Args>(args)...);
    return Client("volume", std::move(client));
  }

  Client
  client(bool new_key = false)
  {
    return client(new_key, {});
  }

  elle::cryptography::rsa::KeyPair owner_keys;
  std::vector<DHT> dhts;
  bool pax;
};

namespace symbols
{
  ELLE_DAS_SYMBOL(str);
  ELLE_DAS_SYMBOL(i64);
  ELLE_DAS_SYMBOL(ui64);
  ELLE_DAS_SYMBOL(b);
  ELLE_DAS_SYMBOL(ri64);
  ELLE_DAS_SYMBOL(rstr);
  ELLE_DAS_SYMBOL(simple);
  ELLE_DAS_SYMBOL(opt_str);
  ELLE_DAS_SYMBOL(opt_simple);
  ELLE_DAS_SYMBOL(rsimple);
  ELLE_DAS_SYMBOL(siopt);
}

namespace structs
{
  using elle::das::operator << ;
  struct Simple
  {
    bool operator == (const Simple& o) const
    { // DAS save me!
      return str == o.str && i64 == o.i64 && ui64 == o.ui64 && b == o.b && ri64 == o.ri64 && rstr == o.rstr;
    };
    std::string str;
    int64_t i64;
    uint64_t ui64;
    bool b;
    std::vector<int64_t> ri64;
    std::vector<std::string> rstr;
    using Model = elle::das::Model<
    Simple,
    decltype(elle::meta::list(::symbols::str,
                              ::symbols::i64, ::symbols::ui64, ::symbols::b,
                              ::symbols::ri64, ::symbols::rstr))>;
  };
}
ELLE_DAS_SERIALIZE(structs::Simple);

ELLE_TEST_SCHEDULED(serialization)
{
  structs::Simple s{"foo", -42, 42, true,
                    std::vector<int64_t>{0, -12, 42},
                    std::vector<std::string>{"foo", "", "bar"}};
  auto reference = s;
  ::Simple sout;
  {
    memo::grpc::SerializerOut ser(&sout);
    ser.serialize_forward(s);
  }
  BOOST_CHECK_EQUAL(sout.str(), "foo");
  BOOST_CHECK_EQUAL(sout.i64(), -42);
  BOOST_CHECK_EQUAL(sout.ui64(), 42);
  BOOST_CHECK_EQUAL(sout.b(), true);
  BOOST_CHECK_EQUAL(sout.ri64_size(), 3);
  s = structs::Simple{"", 0, 0, false};
  {
    memo::grpc::SerializerIn ser(&sout);
    ser.serialize_forward(s);
  }
  BOOST_CHECK_EQUAL(s, reference);
}


namespace structs
{
  struct Complex
  {
    Complex(Simple const& s = Simple{},
            boost::optional<std::string> ost = boost::none,
            boost::optional<Simple> osi = boost::none,
            std::vector<Simple> rs = {},
            elle::Option<std::string, int64_t> sio = (int64_t)0)
    : simple(s)
    , opt_str(ost)
    , opt_simple(osi)
    , rsimple(rs)
    , siopt(sio)
    {}
    bool operator == (const Complex& b) const
    {
      return simple == b.simple
        && opt_str == b.opt_str
        && opt_simple == b.opt_simple
        && rsimple == b.rsimple
        && siopt.is<std::string>() == b.siopt.is<std::string>()
        && (siopt.is<std::string>() ? (siopt.get<std::string>() == b.siopt.get<std::string>())
                                    : (siopt.get<int64_t>() == b.siopt.get<int64_t>()));
    }
    Simple simple;
    boost::optional<std::string> opt_str;
    boost::optional<Simple> opt_simple;
    std::vector<Simple> rsimple;
    elle::Option<std::string, int64_t> siopt;
    using Model = elle::das::Model<
    Complex,
    decltype(elle::meta::list(::symbols::simple,
                              ::symbols::opt_str,
                              ::symbols::opt_simple,
                              ::symbols::rsimple,
                              ::symbols::siopt))>;
  };
}
ELLE_DAS_SERIALIZE(structs::Complex);

ELLE_TEST_SCHEDULED(serialization_complex)
{
  structs::Complex complex {
    structs::Simple{"foo", -42, 42, true},
    std::string("bar"),
    boost::none,
    {},
    std::string("foo")
  };
  ::Complex cplx;
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(cplx.simple().str(), "foo");
  BOOST_CHECK_EQUAL(cplx.simple().i64(), -42);
  BOOST_CHECK_EQUAL(cplx.simple().ui64(), 42);
  BOOST_CHECK_EQUAL(cplx.simple().b(), true);
  BOOST_CHECK_EQUAL(cplx.opt_str(), "bar");
  BOOST_TEST(!cplx.has_opt_simple());
  complex = structs::Complex {
    structs::Simple{"", 0, 0, false},
    std::string()
  };
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(complex.simple.str, "foo");
  BOOST_CHECK_EQUAL(complex.simple.i64, -42);
  BOOST_CHECK_EQUAL(complex.simple.ui64, 42);
  BOOST_CHECK_EQUAL(complex.simple.b, true);
  BOOST_CHECK_EQUAL(complex.opt_str.value_or("UNSET"), "bar");
  BOOST_TEST(!complex.opt_simple);
  // check unset optional<primitive>
  complex.opt_str.reset();
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(cplx.opt_str(), "");
  complex = structs::Complex {
    structs::Simple{"", 0, 0, false},
    std::string("dummy"),
    boost::none,
    {},
    std::string()
  };
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(complex.simple.str, "foo");
  BOOST_CHECK_EQUAL(complex.simple.i64, -42);
  BOOST_CHECK_EQUAL(complex.simple.ui64, 42);
  BOOST_CHECK_EQUAL(complex.simple.b, true);
  // with grpc there is no such concept of optional primitive type, only default value
  BOOST_CHECK_EQUAL(complex.opt_str.value_or("UNSET"), "UNSET");
  // check default value on optional<primitive>
  ELLE_LOG("empty optional<string>");
  complex.opt_str = std::string();
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(cplx.opt_str(), "");
  complex = structs::Complex {
    structs::Simple{"", 0, 0, false},
    std::string("dummy"),
    boost::none,
    {},
    std::string()
  };
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  // FAILS BOOST_CHECK_EQUAL(complex.opt_str.value_or("UNSET"), "");

  BOOST_CHECK(!complex.opt_simple);

  complex.opt_simple = structs::Simple{"foo", -12, 12, true};
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(cplx.opt_str(), "");
  complex = structs::Complex {
    structs::Simple{"", 0, 0, false},
    std::string("dummy"),
    boost::none,
    {},
    std::string()
  };
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK(complex.opt_simple);
  BOOST_CHECK_EQUAL(complex.opt_simple->str, "foo");

  complex.rsimple.push_back(structs::Simple{"foo", -12, 12, true});
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  complex = structs::Complex {
    structs::Simple{"", 0, 0, false},
    std::string("dummy"),
    boost::none,
    {},
    std::string()
  };
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(complex.rsimple.size(), 1);
  BOOST_CHECK_EQUAL(complex.rsimple.front(),
                    (structs::Simple{"foo", -12, 12, true}));
  // Option
  complex.siopt = std::string("foo");
  {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  complex.siopt = (int64_t)42;
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(complex.siopt.get<std::string>(), "foo");
  complex.siopt = (int64_t)42;
    {
    memo::grpc::SerializerOut ser(&cplx);
    ser.serialize_forward(complex);
  }
  complex.siopt = std::string("foo");
  {
    memo::grpc::SerializerIn ser(&cplx);
    ser.serialize_forward(complex);
  }
  BOOST_CHECK_EQUAL(complex.siopt.get<int64_t>(), 42);
}

ELLE_TEST_SCHEDULED(protogen)
{
  Protogen p;
  p.protogen<structs::Complex>();
  for (auto const& m: p.messages)
    BOOST_TEST_MESSAGE(m.second);
}

ELLE_TEST_SCHEDULED(memo_ValueStore_parallel)
{
  // Test GRPC API with multiple concurrent clients
  auto make_kouncil = [](memo::model::doughnut::Doughnut& dht,
                         std::shared_ptr<memo::model::doughnut::Local> local)
  {
    return std::make_unique<memo::overlay::kouncil::Kouncil>(&dht, local);
  };
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto servers = std::vector<std::unique_ptr<DHT>>{};
  for (int i=0; i<3; ++i)
    servers.push_back(std::make_unique<DHT>(
      ::keys = keys,
      ::id = special_id(i+1),
      ::make_overlay = make_kouncil,
      ::paxos = true));
  for (int j=1; j<3; ++j)
    ::discover(*servers[0], *servers[j], false, false, true, true);
  auto client = std::make_unique<DHT>(
      ::keys = keys,
      ::make_overlay = make_kouncil,
      ::paxos = true,
      ::storage = nullptr);
  discover(*client, *servers[0], false);
  elle::reactor::wait(client->dht->overlay()->on_discovery(),
    [&](NodeLocation, bool) { return true;});
  elle::reactor::Barrier b;
  int listening_port = 0;
  auto t = std::make_unique<elle::reactor::Thread>("grpc",
    [&] {
      b.open();
      memo::grpc::serve_grpc(*servers[0]->dht, "127.0.0.1:0",
                                &listening_port);
    });
  elle::reactor::wait(b);
  ELLE_TRACE("will connect to 127.0.0.1:%s", listening_port);
  auto mutable_block = client->dht
    ->make_block<memo::model::blocks::MutableBlock>(std::string("{}"));
  ELLE_TRACE("insert mb");
  client->dht->seal_and_insert(*mutable_block);
  auto task = [&](int id) {
    ELLE_TRACE("%s: create channel", id);
    auto chan = grpc::CreateChannel(
        elle::sprintf("127.0.0.1:%s", listening_port),
        grpc::InsecureChannelCredentials());
    ELLE_TRACE("%s: create stub", id);
    auto stub = ::memo::vs::ValueStore::NewStub(chan);
    ELLE_TRACE("%s: fetch block", id);
    // Fetch mutable_block
    ::memo::vs::Block b;
    {
      grpc::ClientContext context;
      ::memo::vs::FetchResponse abs;
      ::memo::vs::FetchRequest addr;
      addr.set_address(std::string((const char*)mutable_block->address().value(), 32));
      addr.set_decrypt_data(true);
      stub->Fetch(&context, addr, &abs);
      b.CopyFrom(abs.block());
    }
    using Payload = std::unordered_map<std::string, int>;
    namespace json = elle::serialization::json;
    auto payload = json::deserialize<Payload>(b.data_plain());
    auto const sid = elle::sprintf("%s", id);
    // Multiple updates on same block.
    ELLE_LOG("%s update start", id);
    int conflicts = 0;
    for (int i=0; i<20; ++i)
    {
      while (true)
      {
        payload[sid] = i;
        b.set_data_plain(json::serialize(payload).string());
        grpc::ClientContext context;
        ::memo::vs::UpdateResponse repl;
        ::memo::vs::UpdateRequest update;
        update.mutable_block()->CopyFrom(b);
        update.set_decrypt_data(true);
        auto res = stub->Update(&context, update, &repl);
        BOOST_CHECK_EQUAL(res, ::grpc::Status::OK);
        if (repl.has_current())
        {
          b.CopyFrom(repl.current());
          ++conflicts;
          payload = json::deserialize<Payload>(b.data_plain());
          if (i != 0)
            BOOST_CHECK_EQUAL(payload[sid], i-1);
        }
        else
          break;
      }
    }
    ELLE_LOG("%s update end, %s conflicts", id, conflicts);
  };
  ELLE_TRACE("starting tasks");
  elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
  {
    for (int i=0; i<5; ++i)
      s.run_background("task", [&, i] {
          elle::reactor::background([&, i] { task(i);});
      });
    elle::reactor::wait(s);
  };
  ELLE_TRACE("final check");
  auto nb = client->dht->fetch(mutable_block->address());
}

ELLE_TEST_SCHEDULED(memo_ValueStore)
{
  DHTs dhts(3);
  auto client = dhts.client();
  auto const alice = elle::cryptography::rsa::keypair::generate(512);
  auto ubf = std::make_unique<memo::model::doughnut::UB>(
      client.dht.dht.get(), "alice", alice.K(), false);
  auto ubr = std::make_unique<memo::model::doughnut::UB>(
      client.dht.dht.get(), "alice", alice.K(), true);
  client.dht.dht->insert(std::move(ubf));
  client.dht.dht->insert(std::move(ubr));

  elle::reactor::Barrier b;
  int listening_port = 0;
  auto t = std::make_unique<elle::reactor::Thread>("grpc",
    [&] {
      b.open();
      memo::grpc::serve_grpc(*client.dht.dht,
                                "127.0.0.1:0", &listening_port);
    });
  elle::reactor::wait(b);
  ELLE_TRACE("connecting to 127.0.0.1:%s", listening_port);
  auto& sched = elle::reactor::scheduler();
  elle::reactor::background([&] {
    auto chan = grpc::CreateChannel(
        elle::sprintf("127.0.0.1:%s", listening_port),
        grpc::InsecureChannelCredentials());
    auto stub = ::memo::vs::ValueStore::NewStub(chan);
    { // get missing block
      grpc::ClientContext context;
      ::memo::vs::FetchRequest req;
      ::memo::vs::FetchResponse repl;
      req.set_address(
        std::string((const char*)memo::model::Address::null.value(), 32));
      ELLE_LOG("call...");
      auto res = stub->Fetch(&context, req, &repl);
      ELLE_LOG("...called");
      BOOST_CHECK_EQUAL(res, ::grpc::NOT_FOUND);
    }
    { // malformed address
      grpc::ClientContext context;
      ::memo::vs::FetchRequest req;
      ::memo::vs::FetchResponse repl;
      req.set_address("foobar");
      auto res = stub->Fetch(&context, req, &repl);
      BOOST_CHECK_EQUAL(res, ::grpc::INVALID_ARGUMENT);
    }
    // Basic CHB
    ::memo::vs::Block chb;
    { // make
      grpc::ClientContext context;
      ::memo::vs::MakeImmutableBlockRequest data;
      data.set_data("bok");
      stub->MakeImmutableBlock(&context, data, &chb);
      ELLE_LOG("addr: %s", chb.address());
      BOOST_CHECK_EQUAL(chb.address().size(), 32);
      BOOST_CHECK_EQUAL(chb.data(), "bok");
      ELLE_TRACE("addr: %s",
        memo::model::Address((const uint8_t*)chb.address().data()));
    }
    { // store
      grpc::ClientContext context;
      ::memo::vs::InsertResponse repl;
      ::memo::vs::InsertRequest insert;
      insert.mutable_block()->CopyFrom(chb);
      ELLE_LOG("insert, type '%s'", insert.block().type());
      auto res = stub->Insert(&context, insert, &repl);
      ELLE_LOG("...inserted");
      BOOST_CHECK_EQUAL(res, ::grpc::Status::OK);
    }
    // dht fetch check
    auto a = memo::model::Address((const uint8_t*)chb.address().data());
    std::string data;
    sched.mt_run<void>("recheck", [&] {
        auto b = client.dht.dht->fetch(a);
        data = b->data().string();
    });
    BOOST_CHECK_EQUAL(data, "bok");
    { // fetch
      grpc::ClientContext context;
      ::memo::vs::FetchResponse abs;
      ::memo::vs::FetchRequest addr;
      addr.set_address(chb.address());
      stub->Fetch(&context, addr, &abs);
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), chb.address());
      BOOST_CHECK_EQUAL(abs.block().data(), "bok");
    }
    //insert
    {
      grpc::ClientContext context;
      ::memo::vs::InsertImmutableBlockRequest req;
      ::memo::vs::InsertImmutableBlockResponse repl;
      req.set_data("bok bok");
      req.set_owner(std::string(
        (const char*)memo::model::Address::random().value(), 32));
      stub->InsertImmutableBlock(&context, req, &repl);
      ::memo::vs::FetchRequest addr;
      ::memo::vs::FetchResponse abs;
      addr.set_address(repl.address());
      {
        grpc::ClientContext context;
        stub->Fetch(&context, addr, &abs);
      }
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), repl.address());
      BOOST_CHECK_EQUAL(abs.block().data(), "bok bok");
      BOOST_CHECK_EQUAL(abs.block().owner(), req.owner());
    }

    // basic OKB
    ::memo::vs::Block okb;
    { // make
      grpc::ClientContext context;
      ::memo::vs::MakeMutableBlockRequest arg;
      stub->MakeMutableBlock(&context, arg, &okb);
      BOOST_CHECK_EQUAL(okb.address().size(), 32);
      BOOST_CHECK_EQUAL(okb.data(), "");
      ELLE_TRACE("addr: %s",
        memo::model::Address((const uint8_t*)okb.address().data()));
    }
    okb.set_data_plain("bokbok");
    { // store
      grpc::ClientContext context;
      ::memo::vs::InsertRequest insert;
      ::memo::vs::InsertResponse repl;
      insert.mutable_block()->CopyFrom(okb);
      ELLE_LOG("insert, type %s", insert.block().type());
      auto res = stub->Insert(&context, insert, &repl);
      BOOST_CHECK_EQUAL(res, ::grpc::Status::OK);
    }
    ::memo::vs::FetchResponse abs;
    { // fetch
      grpc::ClientContext context;
      ::memo::vs::FetchRequest fetch;
      fetch.set_address(okb.address());
      fetch.set_decrypt_data(true);
      stub->Fetch(&context, fetch, &abs);
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), okb.address());
      BOOST_CHECK_EQUAL(abs.block().data_plain(), "bokbok");
    }
    // update
    {
      abs.mutable_block()->set_data_plain("mooh");
      grpc::ClientContext context;
      ::memo::vs::UpdateResponse repl;
      ::memo::vs::UpdateRequest update;
      update.mutable_block()->CopyFrom(abs.block());
      auto res = stub->Update(&context, update, &repl);
      BOOST_CHECK_EQUAL(res, ::grpc::Status::OK);
    }
    { // fetch
      ::memo::vs::FetchResponse abs; // use another message to be sure
      grpc::ClientContext context;
      ::memo::vs::FetchRequest fetch;
      fetch.set_address(okb.address());
      fetch.set_decrypt_data(true);
      stub->Fetch(&context, fetch, &abs);
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), okb.address());
      BOOST_CHECK_EQUAL(abs.block().data_plain(), "mooh");
    }
    // update again from same object
    {
      abs.mutable_block()->set_data_plain("merow");
      grpc::ClientContext context;
      ::memo::vs::UpdateResponse repl;
      ::memo::vs::UpdateRequest update;
      update.mutable_block()->CopyFrom(abs.block());
      auto res = stub->Update(&context, update, &repl);
      BOOST_CHECK(repl.has_current());
      // retry update
      {
        grpc::ClientContext context;
        ::memo::vs::UpdateRequest update;
        update.mutable_block()->CopyFrom(repl.current());
        update.mutable_block()->set_data_plain("merow");
        res = stub->Update(&context, update, &repl);
        BOOST_CHECK_EQUAL(res, ::grpc::Status::OK);
      }
      { // fetch
        ::memo::vs::FetchResponse tabs;
        grpc::ClientContext context;
        ::memo::vs::FetchRequest fetch;
        fetch.set_address(okb.address());
        fetch.set_decrypt_data(true);
        stub->Fetch(&context, fetch, &tabs);
        BOOST_CHECK(tabs.has_block());
        BOOST_CHECK_EQUAL(tabs.block().data_plain(), "merow");
      }
    }
    //insert
    {
      grpc::ClientContext context;
      ::memo::vs::InsertMutableBlockRequest req;
      ::memo::vs::InsertMutableBlockResponse repl;
      req.set_data("bok bok");
      req.set_owner(std::string(
        (const char*)memo::model::Address::random().value(), 32));
      stub->InsertMutableBlock(&context, req, &repl);
      ::memo::vs::FetchRequest addr;
      ::memo::vs::FetchResponse abs;
      addr.set_address(repl.address());
      addr.set_decrypt_data(true);
      {
        grpc::ClientContext context;
        stub->Fetch(&context, addr, &abs);
      }
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), repl.address());
      BOOST_CHECK_EQUAL(abs.block().data_plain(), "bok bok");
      BOOST_CHECK_EQUAL(abs.block().owner(), req.owner());
    }

    // ACB
#if 0
    ::memo::vs::Block acb;
    { // make
      grpc::ClientContext context;
      ::memo::vs::Empty arg;
      stub->make_acl_block(&context, arg, &acb);
      BOOST_CHECK_EQUAL(acb.address().size(), 32);
      BOOST_CHECK_EQUAL(acb.data(), "");
      ELLE_TRACE("addr: %s",
        memo::model::Address((const uint8_t*)acb.address().data()));
    }
    acb.set_data("bokbok");
    { // store
      grpc::ClientContext context;
      ::memo::vs::Block ab;
      ::memo::vs::EmptyOrException repl;
      ab.CopyFrom(acb);
      stub->insert(&context, ab, &repl);
      BOOST_CHECK_EQUAL(repl.has_exception(), false);
    }
    { // fetch
      grpc::ClientContext context;
      ::memo::vs::Address addr;
      addr.set_address(acb.address());
      stub->fetch(&context, addr, &abs);
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), acb.address());
      BOOST_CHECK_EQUAL(abs.block().data(), "bokbok");
    }
    // update
    {
      abs.mutable_block()->set_data("mooh");
      grpc::ClientContext context;
      ::memo::vs::EmptyOrException repl;
      stub->update(&context, abs.block(), &repl);
      BOOST_CHECK_EQUAL(repl.has_exception(), false);
    }
    { // fetch
      ::memo::vs::BlockOrException abs; // use another message to be sure
      grpc::ClientContext context;
      ::memo::vs::Address addr;
      addr.set_address(acb.address());
      stub->fetch(&context, addr, &abs);
      BOOST_CHECK(abs.has_block());
      BOOST_CHECK_EQUAL(abs.block().address(), acb.address());
      BOOST_CHECK_EQUAL(abs.block().data(), "mooh");
    }
    // update again from same object
    {
      abs.mutable_block()->set_data("merow");
      grpc::ClientContext context;
      ::memo::vs::EmptyOrException repl;
      stub->update(&context, abs.block(), &repl);
      BOOST_CHECK_EQUAL(repl.has_exception(), false);
      { // fetch to get current version
        ::memo::vs::BlockOrException tabs;
        grpc::ClientContext context;
        ::memo::vs::Address addr;
        addr.set_address(acb.address());
        stub->fetch(&context, addr, &tabs);
        BOOST_CHECK(tabs.has_block());
        ELLE_TRACE("update version: %s -> %s",
          abs.block().data_version(),
          tabs.block().data_version());
        abs.mutable_block()->set_data_version(tabs.block().data_version());
      }
      // retry update
      {
        grpc::ClientContext context;
        ::memo::vs::EmptyOrException repl;
        stub->update(&context, abs.block(), &repl);
        BOOST_CHECK_EQUAL(repl.has_exception(), false);
      }
      { // fetch
        grpc::ClientContext context;
        ::memo::vs::Address addr;
        addr.set_address(acb.address());
        stub->fetch(&context, addr, &abs);
        BOOST_CHECK(abs.has_block());
        BOOST_CHECK_EQUAL(abs.block().data(), "merow");
      }
    }
#endif

    // acls
#if 0
    ::memo::vs::KeyOrStatus kohs;
    {
      grpc::ClientContext context;
      ::memo::vs::Bytes name;
      name.set_data("alice");
      stub->UserKey(&context, name, &kohs);
      BOOST_CHECK(kohs.has_key());
    }
    auto* acl = abs.mutable_block()->add_acl();
    acl->set_read(true);
    acl->set_write(true);
    acl->mutable_key_koh()->CopyFrom(kohs.key());
    {
       grpc::ClientContext context;
       ::memo::vs::EmptyOrException status;
       ELLE_TRACE("update with new world perms");
       stub->update(&context, abs.block(), &status);
       BOOST_CHECK_EQUAL(status.has_exception(), false);
    }
    // check read from alice
    sched.mt_run<void>("alice", [&] {
        auto ac = dhts.client(false, alice);
        auto block = ac.dht.dht->fetch(memo::model::Address((uint8_t*)abs.block().address().data()));
        BOOST_CHECK_EQUAL(block->data(), "merow");
    });
#endif
    // username
#if 0
    {
      grpc::ClientContext context;
      ::memo::vs::BytesOrStatus bos;
      stub->UserName(&context, kohs.key(), &bos);
      BOOST_CHECK(bos.has_bytes());
      BOOST_CHECK_EQUAL(bos.bytes().data(), "alice");
    }
#endif

  });
}

ELLE_TEST_SUITE()
{
  auto& master = boost::unit_test::framework::master_test_suite();
  master.add(BOOST_TEST_CASE(serialization), 0, valgrind(10));
  master.add(BOOST_TEST_CASE(serialization_complex), 0, valgrind(10));
  // Takes 13s on a laptop with Valgrind in Docker.  Otherwise less than a sec.
  master.add(BOOST_TEST_CASE(memo_ValueStore), 0, valgrind(20));
  master.add(BOOST_TEST_CASE(memo_ValueStore_parallel), 0, valgrind(60));
  master.add(BOOST_TEST_CASE(protogen), 0, valgrind(10));
  atexit(google::protobuf::ShutdownProtobufLibrary);
}
