#include <elle/log.hh>

#include <infinit/filesystem/filesystem.hh>

#include "DHT.hh" // XXX Shared with tests.

ELLE_LOG_COMPONENT("bench");

using Consensus = infinit::model::doughnut::consensus::Consensus;

class NoCheatConsensus:
  public Consensus
{
public:
  using Super = Consensus;
  NoCheatConsensus(std::unique_ptr<Super> backend)
    : Super(backend->doughnut())
    , _backend(std::move(backend))
  {}

protected:
  std::unique_ptr<infinit::model::doughnut::Local>
  make_local(boost::optional<int> port,
             boost::optional<boost::asio::ip::address> listen,
             std::unique_ptr<infinit::storage::Storage> storage,
             dht::Protocol p) override
  {
    return _backend->make_local(port, listen, std::move(storage), p);
  }

  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address address,
         boost::optional<int> local_version) override
  {
    auto res = _backend->fetch(address, local_version);
    if (res)
    {
      elle::Buffer buf;
      {
        elle::IOStream os(buf.ostreambuf());
        elle::serialization::binary::serialize(res, os);
      }
      elle::IOStream is(buf.istreambuf());
      elle::serialization::Context ctx;
      ctx.set(&doughnut());
      res = elle::serialization::binary::deserialize<
        std::unique_ptr<blocks::Block>>(is, true, ctx);
    }
    return res;
  }

  void
  _store(std::unique_ptr<infinit::model::blocks::Block> block,
    infinit::model::StoreMode mode,
    std::unique_ptr<infinit::model::ConflictResolver> resolver) override
  {
    this->_backend->store(std::move(block), mode, std::move(resolver));
  }

  void
  _remove(infinit::model::Address address,
          infinit::model::blocks::RemoveSignature rs) override
  {
    if (rs.block)
    {
      elle::Buffer buf;
      {
        elle::IOStream os(buf.ostreambuf());
        elle::serialization::binary::serialize(rs.block, os);
      }
      elle::IOStream is(buf.istreambuf());
      elle::serialization::Context ctx;
      ctx.set(&doughnut());
      auto res = elle::serialization::binary::deserialize<
        std::unique_ptr<blocks::Block>>(is, true, ctx);
      rs.block = std::move(res);
    }
    _backend->remove(address, rs);
  }

  std::unique_ptr<Super> _backend;
};

std::unique_ptr<Consensus>
no_cheat_consensus(std::unique_ptr<Consensus> c)
{
  return std::make_unique<NoCheatConsensus>(std::move(c));
}

std::unique_ptr<Consensus>
same_consensus(std::unique_ptr<Consensus> c)
{
  return c;
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
    Client(std::string const& name, DHT dht)
      : dht(std::move(dht))
      , fs(std::make_unique<elle::reactor::filesystem::FileSystem>(
             std::make_unique<infinit::filesystem::FileSystem>(
               name, this->dht.dht,
               infinit::filesystem::allow_root_creation = true),
             true))
    {}

    DHT dht;
    std::unique_ptr<elle::reactor::filesystem::FileSystem> fs;
  };

  template<typename... Args>
  Client
  client(bool new_key,
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
               std::forward<Args>(args)...
               );
    for (auto& dht: this->dhts)
      dht.overlay->connect(*client.overlay);
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

static int64_t block_size = 1024 * 1024; // 1 MiB

static
void
write_test(int64_t total_size)
{
  ELLE_ASSERT_EQ(total_size % block_size, 0);
  ELLE_LOG("write %s MiB with block size of %s MiB",
           total_size / 1024 / 1024, block_size / 1024 / 1024);
  DHTs servers(1);
  auto client = servers.client();
  auto const& root = client.fs->path("/");
  auto handle = root->child("file")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  ELLE_LOG("start write");
  for (int64_t i = 0; i < total_size / block_size; i++)
  {
    handle->write(
      elle::ConstWeakBuffer(std::string(block_size, 'a')),
      block_size, block_size * i);
  }
  handle->fsync(true);
  handle->close();
  ELLE_LOG("done write");
}

int
main(int argc, char const* argv[])
{
  elle::reactor::Scheduler sched;
  elle::reactor::Thread main(
    sched, "main",
    [&]
    {
      write_test(500 * block_size);
    });
  try
  {
    sched.run();
  }
  catch (elle::Error const& e)
  {
    ELLE_ERR("exception escaped: %s", e);
    ELLE_ERR("%s", e.backtrace());
    throw;
  }
  return 0;
}
