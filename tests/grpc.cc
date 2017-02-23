#include <elle/test.hh>

#include <elle/err.hh>

#include <boost/optional.hpp>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>
#include <infinit/storage/MissingKey.hh>

#include <infinit/filesystem/filesystem.hh>

#include <infinit/grpc/kv.grpc.pb.h>
#include <infinit/grpc/fs.grpc.pb.h>

#include <infinit/grpc/grpc.hh>

#include <grpc++/grpc++.h>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test");

class DHTs
{
public:
  template <typename ... Args>
  DHTs(int count)
   : DHTs(count, {})
  {
  }
  template <typename ... Args>
  DHTs(int count,
       boost::optional<elle::cryptography::rsa::KeyPair> kp,
       Args ... args)
    : owner_keys(kp? *kp : elle::cryptography::rsa::keypair::generate(512))
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
      , fs(std::make_unique<elle::reactor::filesystem::FileSystem>(
             std::make_unique<infinit::filesystem::FileSystem>(
               name, this->dht.dht, infinit::filesystem::allow_root_creation = true,
               std::forward<Args>(args)...),
             true))
    {}

    DHT dht;
    std::unique_ptr<elle::reactor::filesystem::FileSystem> fs;
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
               make_consensus = no_cheat_consensus,
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


ELLE_TEST_SCHEDULED(basic)
{
  DHTs dhts(3);
  auto client = dhts.client();
  auto alice = elle::cryptography::rsa::keypair::generate(512);
  auto ubf = infinit::model::doughnut::UB(
    client.dht.dht.get(), "alice", alice.K(), false);
  auto ubr = infinit::model::doughnut::UB(
    client.dht.dht.get(), "alice", alice.K(), true);
  client.dht.dht->store(ubf, infinit::model::STORE_INSERT);
  client.dht.dht->store(ubr, infinit::model::STORE_INSERT);
  infinit::model::Endpoint ep("127.0.0.1", (rand()%10000)+50000);
  elle::reactor::Barrier b;
  auto t = std::make_unique<elle::reactor::Thread>("grpc",
    [&] {
      ELLE_LOG("open");
      b.open();
      ELLE_LOG("serve");
      infinit::grpc::serve_grpc(*client.dht.dht, boost::none, ep);
      ELLE_LOG("done");
    });
  ELLE_LOG("wait");
  elle::reactor::wait(b);
  ELLE_LOG("start");
  elle::reactor::background([&] {
  auto chan = grpc::CreateChannel(
      elle::sprintf("127.0.0.1:%s", ep.port()),
      grpc::InsecureChannelCredentials());
  auto stub = KV::NewStub(chan);
  { // get missing block
    grpc::ClientContext context;
    ::Address req;
    ::BlockStatus repl;
    req.set_address(elle::sprintf("%s", infinit::model::Address::null));
    ELLE_LOG("call...");
    auto res = stub->Get(&context, req, &repl);
    ELLE_LOG("...called");
    //BOOST_CHECK_EQUAL((int)res, (int)::grpc::Status::OK);
    BOOST_CHECK_EQUAL(repl.status().error(), ERROR_MISSING_BLOCK);
  }
  { // put/get chb
    grpc::ClientContext context;
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_constant_block();
    ::Status repl;
    stub->Set(&context, req, &repl);
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    {
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
    }
    BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
    BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    { // STORE_UPDATE chb
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_NO_PEERS);
    }
  }
  { // mb
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_mutable_block();
    ::Status repl;
    { // insert
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    }
    { // update
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      req.mutable_block()->set_address(addr.address());
      {
        grpc::ClientContext context;
        stub->Set(&context, req, &repl);
        BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
      }
    }
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().address(), addr.address());
    }
  }
  { // acb
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_acl_block();
    ::Status repl;
    { // insert
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    }
    { // update
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      req.mutable_block()->set_address(addr.address());
      {
        grpc::ClientContext context;
        stub->Set(&context, req, &repl);
        BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
      }
    }
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().address(), addr.address());
    }
    // world perms
    BOOST_CHECK_EQUAL(bs.block().acl_block().world_read(), false);
    BOOST_CHECK_EQUAL(bs.block().acl_block().world_write(), false);
    req.mutable_block()->mutable_acl_block()->set_world_read(true);
    req.mutable_block()->mutable_acl_block()->set_world_write(true);
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    {
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().acl_block().world_read(), true);
      BOOST_CHECK_EQUAL(bs.block().acl_block().world_write(), true);
    }
    // version conflict
    req.mutable_block()->mutable_acl_block()->set_version(1);
    req.mutable_block()->set_payload("baz");
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_CONFLICT);
    }
    req.mutable_block()->mutable_acl_block()->set_version(repl.version()+1);
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    // acls
    req.mutable_block()->mutable_acl_block()->set_version(0);
    { // add alice
      auto acl = req.mutable_block()->mutable_acl_block()->add_permissions();
      acl->set_user("alice");
      acl->set_read(true);
      acl->set_write(true);
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    { // check alice
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "baz");
      BOOST_CHECK_EQUAL(bs.block().acl_block().permissions_size(), 2);
      auto const& p = bs.block().acl_block().permissions(1);
      BOOST_CHECK_EQUAL(p.user(), "alice");
      BOOST_CHECK_EQUAL(p.admin(), false);
      BOOST_CHECK_EQUAL(p.owner(), false);
      BOOST_CHECK_EQUAL(p.read(), true);
      BOOST_CHECK_EQUAL(p.write(), true);
    }
    { // remove alice
      auto acl = req.mutable_block()->mutable_acl_block()->mutable_permissions(0);
      acl->set_read(false);
      acl->set_write(false);
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    { // check
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().acl_block().permissions_size(), 1);
    }
  }});
  ELLE_LOG("done");
}

ELLE_TEST_SCHEDULED(filesystem)
{
  DHTs dhts(3);
  auto client = dhts.client();
  infinit::model::Endpoint ep("127.0.0.1", (rand()%10000)+50000);
  elle::reactor::Barrier b;
  auto t = std::make_unique<elle::reactor::Thread>("grpc",
    [&] {
      ELLE_LOG("open");
      b.open();
      ELLE_LOG("serve");
      infinit::grpc::serve_grpc(*client.dht.dht, *client.fs, ep);
      ELLE_LOG("done");
    });
  ELLE_LOG("wait");
  elle::reactor::wait(b);
  ELLE_LOG("start");
  elle::reactor::background([&] {
  auto chan = grpc::CreateChannel(
      elle::sprintf("127.0.0.1:%s", ep.port()),
      grpc::InsecureChannelCredentials());
  auto stub = FileSystem::NewStub(chan);
  ::Path path;
  ::FsStatus status;
  { // list /
    path.set_path("/");
    ::DirectoryContent dc;
    grpc::ClientContext context;
    stub->ListDir(&context, path, &dc);
    BOOST_CHECK_EQUAL(dc.status().code(), 0);
    BOOST_CHECK_EQUAL(dc.content_size(), 2);
  }
  // dirs
  path.set_path("/foo");
  { // mkdir
    grpc::ClientContext context;
    stub->MkDir(&context, path, &status);
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // check
    path.set_path("/");
    ::DirectoryContent dc;
    grpc::ClientContext context;
    stub->ListDir(&context, path, &dc);
    BOOST_CHECK_EQUAL(dc.status().code(), 0);
    BOOST_CHECK_EQUAL(dc.content_size(), 3);
    BOOST_CHECK_EQUAL(dc.content(2).name(), "foo");
    BOOST_CHECK_EQUAL(dc.content(2).type(), ENTRY_DIRECTORY);
  }
  { // rmdir
    path.set_path("/foo");
    grpc::ClientContext context;
    stub->RmDir(&context, path, &status);
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // check
    path.set_path("/");
    ::DirectoryContent dc;
    grpc::ClientContext context;
    stub->ListDir(&context, path, &dc);
    BOOST_CHECK_EQUAL(dc.status().code(), 0);
    BOOST_CHECK_EQUAL(dc.content_size(), 2);
  }
  // files
  ::Handle handle;
  ::StatusHandle sh;
  ::HandleBuffer hb;
  ::StatusBuffer sb;
  ::HandleRange hr;
  { // open
    path.set_path("/bar");
    grpc::ClientContext context;
    stub->OpenFile(&context, path, &sh);
    BOOST_CHECK_EQUAL(sh.status().code(), 0);
  }
  { // write
    hb.mutable_handle()->set_handle(sh.handle().handle());
    hb.mutable_buffer()->set_data("barbarbar");
    grpc::ClientContext context;
    stub->Write(&context, hb, &status);
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // close
    grpc::ClientContext context;
    stub->CloseFile(&context, sh.handle(), &status);
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // open
    grpc::ClientContext context;
    stub->OpenFile(&context, path, &sh);
    BOOST_CHECK_EQUAL(sh.status().code(), 0);
  }
  { // read
    hr.mutable_handle()->set_handle(sh.handle().handle());
    hr.mutable_range()->set_size(1000);
    hr.mutable_range()->set_offset(1);
    grpc::ClientContext context;
    stub->Read(&context, hr, &sb);
    BOOST_CHECK_EQUAL(sb.status().code(), 0);
    BOOST_CHECK_EQUAL(sb.buffer().data(), "arbarbar");
  }
  { // open
    path.set_path("/stream");
    grpc::ClientContext context;
    stub->OpenFile(&context, path, &sh);
    BOOST_CHECK_EQUAL(sh.status().code(), 0);
  }
  { // write stream
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientWriter< ::HandleBuffer> > writer(
      stub->WriteStream(&context, &status));
    hb.mutable_handle()->set_handle(sh.handle().handle());
    for (int i = 0; i< 67; ++i)
    {
      hb.mutable_buffer()->set_offset(16384 * i);
      hb.mutable_buffer()->set_data(std::string(16384, 'a'));
      writer->Write(hb);
    }
    writer->WritesDone();
    writer->Finish();
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // close
    grpc::ClientContext context;
    stub->CloseFile(&context, sh.handle(), &status);
    BOOST_CHECK_EQUAL(status.code(), 0);
  }
  { // open
    grpc::ClientContext context;
    stub->OpenFile(&context, path, &sh);
    BOOST_CHECK_EQUAL(sh.status().code(), 0);
  }
  { // read stream
    hr.mutable_handle()->set_handle(sh.handle().handle());
    hr.mutable_range()->set_size(-1);
    hr.mutable_range()->set_offset(1);
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientReader< ::StatusBuffer> > reader(
      stub->ReadStream(&context, hr));
    std::string payload;
    while (reader->Read(&sb))
    {
      BOOST_CHECK_EQUAL(sb.status().code(), 0);
      payload += sb.buffer().data();
    }
    reader->Finish();
    BOOST_CHECK_EQUAL(payload.size(), 67 * 16384 - 1);
  }
  });
}

ELLE_TEST_SUITE()
{
  auto& master = boost::unit_test::framework::master_test_suite();
  master.add(BOOST_TEST_CASE(basic), 0, valgrind(10));
  master.add(BOOST_TEST_CASE(filesystem), 0, valgrind(60));
}