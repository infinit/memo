#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>

#include <elle/memory.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <elle/cryptography/SecretKey.hh>

#include <memo/model/blocks/Block.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/doughnut/CHB.hh>
#include <memo/model/doughnut/NB.hh>
#include <memo/model/doughnut/OKB.hh>
#include <memo/model/doughnut/UB.hh>
#include <memo/filesystem/filesystem.hh>
#include <memo/filesystem/Directory.hh>
#include <memo/filesystem/File.hh>
#include <memo/utility.hh>

ELLE_LOG_COMPONENT("backward-compatibility");

namespace bfs = boost::filesystem;

static
bfs::path
root()
{
  static bfs::path source(elle::os::getenv("SOURCE_DIR", "."));
  auto res = source / "tests/backward-compatibility";
  if (!bfs::exists(res))
    res = source / "backend/tests/backward-compatibility";
  return res;
}

static
bfs::path
path(elle::Version const& v)
{
  return root() / elle::sprintf("%s", v);
}

static const elle::Buffer private_key(
  "{\".version\":\"0.0.0\",\"rsa\":\"MIIBOwIBAAJBAMqLF/k4SGtyybd1f3TsUF7ahLTB"
  "egwtKDPvcB/hnKMrqlAgttE4P6b1AuJBkNpLF+VWdVHLoZmfKMyANtNwLK8CAwEAAQJAA8kvzI"
  "fByshdfuFiXYQhSHSbMGnBZ0Lc0oOyO9ZSwDYDOC/2tm/7hAnpVBdfaYiDSijDDyxjFEy6/ZZO"
  "jUw5sQIhAOjOil4jXB+od5YINjSXSjyZwwizElUomJxyzLpGruHJAiEA3ri4ONSVtWj68fieKv"
  "ZS0EmvLESPcysOo0WTsUuZlrcCIQDN07212SFbxABmnz/9Yzz5MyCiEmBE9i1nNIAYuOFpMQIh"
  "AIiB3ye15CxUM7qrDwZ2Azv2bY9MVj/YXBhmRKeeFnzxAiAvIkam7Hl7mVknA8EDZ0eK0R7RVD"
  "RCAVsIFJOdtF0Rfg==\"}");

static const elle::Buffer public_key(
  "{\".version\":\"0.0.0\",\"rsa\":\"MEgCQQDKixf5OEhrcsm3dX907FBe2oS0wXoMLSgz"
  "73Af4ZyjK6pQILbROD+m9QLiQZDaSxflVnVRy6GZnyjMgDbTcCyvAgMBAAE=\"}}");

static const elle::Buffer secret_buffer(
  "{\".version\":\"0.0.0\",\"password\":\"wKJEI6V8BKyNZfjd0xGZsY55b5tvYAO3R/b"
  "WfHvUOnQ=\"}");

class DeterministicPublicKey
  : public elle::cryptography::rsa::PublicKey
{
public:
  using Super = elle::cryptography::rsa::PublicKey;
  using Super::Super;

  DeterministicPublicKey(Super const& model)
    : Super(model)
  {}

  virtual
  elle::Buffer
  seal(elle::ConstWeakBuffer const& plain,
       elle::cryptography::Cipher const cipher,
       elle::cryptography::Mode const mode) const override
  {
    return elle::Buffer(plain);
  }

  virtual
  elle::Buffer
  encrypt(elle::ConstWeakBuffer const& plain,
          elle::cryptography::rsa::Padding const padding) const override
  {
    return elle::Buffer(plain);
  }

  virtual
  bool
  _verify(elle::ConstWeakBuffer const& signature,
         elle::ConstWeakBuffer const& plain,
         elle::cryptography::rsa::Padding const,
         elle::cryptography::Oneway const) const override
  {
    return signature == plain;
  }
};

class DeterministicPrivateKey
  : public elle::cryptography::rsa::PrivateKey
{
public:
  using Super = elle::cryptography::rsa::PrivateKey;
  using Super::Super;

  virtual
  elle::Buffer
  sign(elle::ConstWeakBuffer const& plain,
       elle::cryptography::rsa::Padding const padding,
       elle::cryptography::Oneway const oneway) const override
  {
    return elle::Buffer(plain);
  }
};

class DeterministicSecretKey
  : public elle::cryptography::SecretKey
{
public:
  using Super = elle::cryptography::SecretKey;
  using Super::Super;

  virtual
  void
  encipher(std::istream& plain,
           std::ostream& code,
           elle::cryptography::Cipher const cipher,
           elle::cryptography::Mode const mode,
           elle::cryptography::Oneway const oneway) const override
  {
    std::copy(std::istreambuf_iterator<char>(plain),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(code));
  }
};

static const elle::Buffer salt("HARDCODED_SALT");
static const DeterministicSecretKey secret =
  elle::serialization::json::deserialize<DeterministicSecretKey>(secret_buffer);

namespace dht = memo::model::doughnut;
namespace fs = memo::filesystem;

class DummyDoughnut
  : public dht::Doughnut
{
public:
  DummyDoughnut(std::shared_ptr<elle::cryptography::rsa::KeyPair> keys,
                boost::optional<elle::Version> v)
    : dht::Doughnut(
      memo::model::Address::null, keys, keys->public_key(),
      dht::Passport(keys->K(), "network", *keys),
      [] (dht::Doughnut&)
      { return nullptr; },
      [] (dht::Doughnut&, std::shared_ptr<dht::Local>)
      { return nullptr; },
      dht::version = v)
  {}
};

struct TestSet
{
  TestSet(std::shared_ptr<elle::cryptography::rsa::KeyPair> keys,
          boost::optional<elle::Version> v)
    : dht(keys, std::move(v))
    , chb(new dht::CHB(&dht, std::string("CHB contents"), salt))
    , acb(new dht::ACB(&dht, std::string("ACB contents"), salt))
    , okb(new dht::OKB(&dht, std::string("OKB contents"), salt))
    , nb(new dht::NB(dht, "NB name", std::string("NB contents")))
    , ub(new dht::UB(&dht, "USERNAME", keys->K(), false))
    , rub(new dht::UB(&dht, "USERNAME", keys->K(), true))
  {
    chb->seal();
    acb->seal({}, secret);
    okb->seal();
    nb->seal();
    ub->seal();
    rub->seal();
  }

  void apply(std::string const& action,
             std::function<void(std::string const&,
                                memo::model::blocks::Block*)> const& f)
  {
    ELLE_LOG("%s CHB", action)
      f("CHB", chb.get());
    ELLE_LOG("%s OKB", action)
      f("OKB", okb.get());
    ELLE_LOG("%s ACB", action)
      f("ACB", acb.get());
    ELLE_LOG("%s NB" , action)
      f("NB",  nb.get());
    ELLE_LOG("%s UB" , action)
      f("UB",  ub.get());
    ELLE_LOG("%s RUB", action)
      f("RUB",  rub.get());
  };

  DummyDoughnut dht;
  std::shared_ptr<dht::CHB> chb;
  std::shared_ptr<dht::ACB> acb;
  std::shared_ptr<dht::OKB> okb;
  std::shared_ptr<dht::NB> nb;
  std::shared_ptr<dht::UB> ub;
  std::shared_ptr<dht::UB> rub;
};

struct TestSetConflictResolver
{
  TestSetConflictResolver(std::shared_ptr<elle::cryptography::rsa::KeyPair> keys,
                          boost::optional<elle::Version> v)
  : dht(keys, v)
  , dir(new fs::DirectoryConflictResolver(dht,
      fs::Operation{fs::OperationType::insert, "foo", fs::EntryType::directory, {}},
      memo::model::Address()))
  , file(new fs::FileConflictResolver("/foo", &dht, fs::WriteTarget::data))
  {}
  void apply(std::string const& action,
             std::function<void(std::string const&,
                                memo::model::ConflictResolver*)> const& f)
  {
    std::cout << "  " << action << " dir" << std::endl;
    f("cr_dir", dir.get());
    std::cout << "  " << action << " file" << std::endl;
    f("cr_file", file.get());
  }
  DummyDoughnut dht;
  std::shared_ptr<fs::DirectoryConflictResolver> dir;
  std::shared_ptr<fs::FileConflictResolver> file;
};

template<typename Base>
void
generate(std::string const& name, Base* b, bfs::path dir,
         elle::Version const& version)
{
  {
    auto path = dir / elle::sprintf("%s.bin", name);
    bfs::ofstream output(path);
    if (!output.good())
       elle::err("unable to open %s", path);
    elle::serialization::binary::serialize(b, output, version, false);
  }
  {
    auto path = dir / elle::sprintf("%s.json", name);
    bfs::ofstream output(path);
    if (!output.good())
      elle::err("unable to open %s", path);
    elle::serialization::json::serialize(b, output, version, false);
  }
}

int
main(int argc, char** argv)
{
  boost::program_options::options_description options("Options");
  options.add_options()
    ("help,h", "display the help")
    ("version,v", "display version")
    ("generate,g", "generate serialized data instead of checking")
    ;
  auto parser = boost::program_options::command_line_parser(argc, argv);
  parser.options(options);
  auto parsed = parser.run();
  boost::program_options::variables_map vm;
  store(parsed, vm);
  notify(vm);
  if (vm.count("help"))
  {
    std::cout << "Usage: " << argv[0] << " [OPTIONS...]" << std::endl;
    std::cout << std::endl;
    std::cout << options;
    std::cout << std::endl;
    return 0;
  }
  if (vm.count("version"))
  {
    std::cout << memo::version() << std::endl;
    return 0;
  }
  elle::reactor::Scheduler sched;
  elle::reactor::Thread main(
    sched,
    "main",
    [&]
    {
      auto K = elle::serialization::json::deserialize
        <std::shared_ptr<DeterministicPublicKey>>(public_key);
      auto k = elle::serialization::json::deserialize
        <std::shared_ptr<DeterministicPrivateKey>>(private_key);
      auto keys = std::make_shared
        <elle::cryptography::rsa::KeyPair>(K, k);
      if (vm.count("generate"))
      {
        elle::Version current_version(
          memo::version().major(), memo::version().minor(), 0);
        auto current = path(current_version);
        std::cout << "Create reference data for version "
                  << current_version << std::endl;
        bfs::create_directories(current);
        TestSet set(keys, current_version);
        set.apply(
          "generate",
          [&] (std::string const& name, memo::model::blocks::Block* b)
          {
            generate(name, b, current, current_version);
          });
        TestSetConflictResolver cr(keys, current_version);
        cr.apply(
          "generate",
          [&] (std::string const& name, memo::model::ConflictResolver* b)
          {
            generate(name, b, current, current_version);
          });
      }
      else
      {
        for (auto const& p: bfs::directory_iterator(root()))
        {
          auto const version = [&]
          {
            auto const filename = p.path().filename().string();
            try
            {
              auto const dot = filename.find('.');
              if (dot == std::string::npos)
                throw std::runtime_error("override me I'm famous");
              auto const major = std::stoi(filename.substr(0, dot));
              auto const minor = std::stoi(filename.substr(dot + 1));
              return elle::Version(major, minor, 0);
            }
            catch (std::exception const&)
            {
              elle::err("invalid directory in %s: %s",
                        root(), filename);
            }
          }();
          ELLE_LOG_SCOPE(
            "check backward compatibility with version %s", version);
          TestSet set(keys, version);
          set.apply(
            "check",
            [&] (std::string const& name, memo::model::blocks::Block* b)
            {
              {
                auto path = p / elle::sprintf("%s.json", name);
                bfs::ifstream input(path);
                if (!input.good())
                  elle::err("unable to open %s", path);
                auto contents = elle::Buffer(
                  std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::json::serialize(b, version, false));
                elle::serialization::Context ctx;
                ctx.set<dht::Doughnut*>(&set.dht);
                auto loaded =
                  elle::serialization::json::deserialize<
                  std::unique_ptr<memo::model::blocks::Block>>(
                    contents, version, false, ctx);
                // Replace OKB owner keys with deterministic ones
                std::cerr << "CAST" << std::endl;
                if (auto okb = std::dynamic_pointer_cast<dht::OKB>(loaded))
                {
                  std::cerr << "A" << std::endl;
                  elle::unconst(okb->owner_key()) =
                    std::make_shared<DeterministicPublicKey>(
                      *std::move(okb->owner_key()));
                  loaded = std::move(okb);
                }
                else if (auto acb = std::dynamic_pointer_cast<dht::ACB>(loaded))
                {
                  std::cerr << "B" << std::endl;
                  elle::unconst(acb->owner_key()) =
                    std::make_shared<DeterministicPublicKey>(
                      *std::move(acb->owner_key()));
                  loaded = std::move(acb);
                }
                else if (auto nb = std::dynamic_pointer_cast<dht::NB>(loaded))
                {
                  std::cerr << "C" << std::endl;
                  elle::unconst(nb->owner()) =
                    std::make_shared<DeterministicPublicKey>(
                      *std::move(nb->owner()));
                  loaded = std::move(nb);
                }
                ELLE_ASSERT(loaded->validate(set.dht, false));
              }
              {
                auto path = p / elle::sprintf("%s.bin", name);
                bfs::ifstream input(path, std::ios::binary);
                if (!input.good())
                  elle::err("unable to open %s", path);
                auto contents = elle::Buffer(
                  std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::binary::serialize(b, version, false));
              }
            });
          TestSetConflictResolver cr(keys, version);
          cr.apply(
            "check",
            [&] (std::string const& name, memo::model::ConflictResolver* b)
            {
              {
                auto path = p / elle::sprintf("%s.json", name);
                bfs::ifstream input(path);
                if (!input.good())
                {
                  ELLE_WARN("unable to open %s", path);
                  return;
                }
                auto contents = elle::Buffer(
                  std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::json::serialize(b, version, false));
              }
              {
                auto path = p / elle::sprintf("%s.bin", name);
                bfs::ifstream input(path, std::ios::binary);
                if (!input.good())
                {
                  ELLE_WARN("unable to open %s", path);
                  return;
                }
                auto contents = elle::Buffer(
                  std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::binary::serialize(b, version, false));
              }
            });
        }
      }
    });
  try
  {
    sched.run();
  }
  catch (elle::Error const& e)
  {
    std::cerr << argv[0] << ": " << e.what() << std::endl;
    return 1;
  }
}
