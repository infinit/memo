#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>

#include <elle/memory.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <cryptography/SecretKey.hh>

#include <infinit/model/blocks/Block.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("backward_compatibility");

static
boost::filesystem::path
root()
{
  static boost::filesystem::path source(elle::os::getenv("SOURCE_DIR", "."));
  auto res = source / "tests/backward-compatibility";
  if (!boost::filesystem::exists(res))
    res = source / "backend/tests/backward-compatibility";
  return res;
}

static
boost::filesystem::path
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
  : public infinit::cryptography::rsa::PublicKey
{
public:
  typedef infinit::cryptography::rsa::PublicKey Super;
  using Super::Super;

  virtual
  elle::Buffer
  seal(elle::ConstWeakBuffer const& plain,
       infinit::cryptography::Cipher const cipher,
       infinit::cryptography::Mode const mode) const override
  {
    return elle::Buffer(plain);
  }
};

class DeterministicPrivateKey
  : public infinit::cryptography::rsa::PrivateKey
{
public:
  typedef infinit::cryptography::rsa::PrivateKey Super;
  using Super::Super;

  virtual
  elle::Buffer
  sign(elle::ConstWeakBuffer const& plain,
       infinit::cryptography::rsa::Padding const padding,
       infinit::cryptography::Oneway const oneway) const override
  {
    return elle::Buffer(plain);
  }
};

class DeterministicSecretKey
  : public infinit::cryptography::SecretKey
{
public:
  typedef infinit::cryptography::SecretKey Super;
  using Super::Super;

  virtual
  void
  encipher(std::istream& plain,
           std::ostream& code,
           infinit::cryptography::Cipher const cipher,
           infinit::cryptography::Mode const mode,
           infinit::cryptography::Oneway const oneway) const override
  {
    std::copy(std::istreambuf_iterator<char>(plain),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(code));
  }
};

class DummyDoughnut
  : public infinit::model::doughnut::Doughnut
{
public:

  DummyDoughnut(infinit::model::Address id,
                std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys)
    : infinit::model::doughnut::Doughnut(
      id, keys, keys->K(),
      infinit::model::doughnut::Passport(keys->K(), "network", keys->k()),
      [] (infinit::model::doughnut::Doughnut&)
      { return nullptr; },
      [] (infinit::model::doughnut::Doughnut&, infinit::model::Address, std::shared_ptr<infinit::model::doughnut::Local>)
      { return nullptr; },
      {}, nullptr)
  {
  }
};

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
    std::cout << INFINIT_VERSION << std::endl;
    return 0;
  }
  reactor::Scheduler sched;
  reactor::Thread main(
    sched,
    "main",
    [&]
    {
      static const elle::Buffer salt("HARDCODED_SALT");
      auto secret =
        elle::serialization::json::deserialize<DeterministicSecretKey>
        (secret_buffer);
      auto K = elle::serialization::json::deserialize
        <std::shared_ptr<DeterministicPublicKey>>(public_key);
      auto k = elle::serialization::json::deserialize
        <std::shared_ptr<DeterministicPrivateKey>>(private_key);
      auto keys = std::make_shared
        <infinit::cryptography::rsa::KeyPair>(K, k);
      DummyDoughnut dht(infinit::model::Address::null, keys);
      auto chb =
        new infinit::model::doughnut::CHB(&dht, std::string("CHB contents"), salt);
      chb->seal();
      auto acb = new infinit::model::doughnut::ACB(
        &dht, std::string("ACB contents"), salt);
      acb->seal(secret);
      auto okb = new infinit::model::doughnut::OKB(
        &dht, std::string("OKB contents"), salt);
      okb->seal();
      auto nb = new infinit::model::doughnut::NB(
        &dht, keys->K(), "NB name", std::string("NB contents"));
      nb->seal();
      auto ub = new infinit::model::doughnut::UB(&dht, "USERNAME", keys->K(), false);
      ub->seal();
      auto rub = new infinit::model::doughnut::UB(&dht, "USERNAME", keys->K(), true);
      rub->seal();
      auto apply =
        [&] (std::string const& action,
             std::function<void(std::string const&,
                                infinit::model::blocks::Block*)> const& f)
        {
          std::cout << "  " << action << " CHB" << std::endl;
          f("CHB", chb);
          std::cout << "  " << action << " OKB" << std::endl;
          f("OKB", okb);
          std::cout << "  " << action << " ACB" << std::endl;
          f("ACB", acb);
          std::cout << "  " << action << " NB" << std::endl;
          f("NB",  nb);
          std::cout << "  " << action << " UB" << std::endl;
          f("UB",  ub);
          std::cout << "  " << action << " RUB" << std::endl;
          f("RUB",  rub);
        };
      if (vm.count("generate"))
      {
        elle::Version current_version(INFINIT_MAJOR, INFINIT_MINOR, 0);
        auto current = path(current_version);
        std::cout << "Create reference data for version "
                  << current_version << std::endl;
        boost::filesystem::create_directories(current);
        auto generate =
          [&] (std::string const& name, infinit::model::blocks::Block* b)
          {
            {
              auto path = current / elle::sprintf("%s.bin", name);
              boost::filesystem::ofstream output(path);
              if (!output.good())
                throw elle::Error(elle::sprintf("unable to open %s", path));
              elle::serialization::binary::serialize(b, output, false);
            }
            {
              auto path = current / elle::sprintf("%s.json", name);
              boost::filesystem::ofstream output(path);
              if (!output.good())
                throw elle::Error(elle::sprintf("unable to open %s", path));
              elle::serialization::json::serialize(b, output, false);
            }
          };
        apply("generate", generate);
      }
      else
      {
        for (auto it = boost::filesystem::directory_iterator(root());
             it != boost::filesystem::directory_iterator();
             ++it)
        {
          auto version = [&] ()
          {
            auto filename = it->path().filename().string();
            try
            {
              auto dot = filename.find('.');
              if (dot == std::string::npos)
                throw std::runtime_error("override me I'm famous");
              auto major = std::stoi(filename.substr(0, dot));
              auto minor = std::stoi(filename.substr(dot + 1));
              return elle::Version(major, minor, 0);
            }
            catch (std::exception const&)
            {
              throw elle::Error(elle::sprintf("invalid directory in %s: %s",
                                              root(), filename));
            }
          }();
          std::cout << "check backward compatibility with version "
                    << version << std::endl;
          auto check =
            [&] (std::string const& name, infinit::model::blocks::Block* b)
            {
              {
                auto path = it->path() / elle::sprintf("%s.json", name);
                boost::filesystem::ifstream input(path);
                if (!input.good())
                  throw elle::Error(elle::sprintf("unable to open %s", path));
                elle::Buffer contents(
                  std::string((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::json::serialize(b, version, false));
              }
              {
                auto path = it->path() / elle::sprintf("%s.bin", name);
                boost::filesystem::ifstream input(path);
                if (!input.good())
                  throw elle::Error(elle::sprintf("unable to open %s", path));
                elle::Buffer contents(
                  std::string((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>()));
                ELLE_ASSERT_EQ(
                  contents,
                  elle::serialization::binary::serialize(b, version, false));
              }
            };
          apply("check", check);
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
