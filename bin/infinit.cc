#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>

#include <elle/Error.hh>
#include <elle/printf.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/model/paranoid/Paranoid.hh>
#include <infinit/overlay/Overlay.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Async.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit");

boost::optional<std::string> root_address_file;

/*----------------------.
| Storage configuration |
`----------------------*/

namespace infinit
{
  namespace storage
  {

    struct MemoryStorageConfig:
      public StorageConfig
    {
    public:
      MemoryStorageConfig(elle::serialization::SerializerIn& input)
        : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s)
      {}

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Memory>();
      }
    };
    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<MemoryStorageConfig> _register_MemoryStorageConfig("memory");

    struct FilesystemStorageConfig:
      public StorageConfig
    {
    public:
      std::string path;

      FilesystemStorageConfig(elle::serialization::SerializerIn& input)
        : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("path", this->path);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Filesystem>(this->path);
      }
    };
    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<FilesystemStorageConfig>
    _register_FilesystemStorageConfig("filesystem");
  }
}


struct OverlayConfig:
  public elle::serialization::VirtuallySerializable
{
  static constexpr char const* virtually_serializable_key = "type";

  virtual
  std::unique_ptr<infinit::overlay::Overlay>
  make() = 0;
};

struct KelipsOverlayConfig:
  public OverlayConfig
{
  KelipsOverlayConfig(elle::serialization::SerializerIn& input)
    : OverlayConfig()
  {
    this->serialize(input);
  }
  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("storage", this->storage);
    s.serialize("config", this->config);
  }
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  kelips::Configuration config;
  virtual
  std::unique_ptr<infinit::overlay::Overlay>
  make()
  {
    std::unique_ptr<infinit::storage::Storage> s = storage->make();
    return elle::make_unique<kelips::Node>(config, std::move(s));
  }
};
static const elle::serialization::Hierarchy<OverlayConfig>::
Register<KelipsOverlayConfig> _registerKelipsOverlayConfig("kelips");


struct KademliaOverlayConfig:
  public OverlayConfig
{
  KademliaOverlayConfig(elle::serialization::SerializerIn& input)
    : OverlayConfig()
  {
    this->serialize(input);
  }
  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("storage", this->storage);
    s.serialize("config", this->config);
  }
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  kademlia::Configuration config;
  virtual
  std::unique_ptr<infinit::overlay::Overlay>
  make()
  {
    std::unique_ptr<infinit::storage::Storage> s = storage->make();
    return elle::make_unique<kademlia::Kademlia>(config, std::move(s));
  }
};
static const elle::serialization::Hierarchy<OverlayConfig>::
Register<KademliaOverlayConfig> _registerKademliaOverlayConfig("kademlia");



struct StonehengeOverlayConfig:
  public OverlayConfig
{
  std::vector<std::string> nodes;
  StonehengeOverlayConfig(elle::serialization::SerializerIn& input)
    : OverlayConfig()
  {
    this->serialize(input);
  }
  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("nodes", this->nodes);
  }
  virtual
  std::unique_ptr<infinit::overlay::Overlay>
  make()
  {
    infinit::overlay::Overlay::Members members;
    for (auto const& hostport: nodes)
    {
      size_t p = hostport.find_first_of(':');
      if (p == hostport.npos)
        throw std::runtime_error("Failed to parse host:port " + hostport);
      members.emplace_back(boost::asio::ip::address::from_string(hostport.substr(0, p)),
                           std::stoi(hostport.substr(p+1)));
    }
    return elle::make_unique<infinit::overlay::Stonehenge>(members);
  }
};
static const elle::serialization::Hierarchy<OverlayConfig>::
Register<StonehengeOverlayConfig> _registerStonehengeOverlayConfig("stonehenge");

/*--------------------.
| Model configuration |
`--------------------*/

struct ModelConfig:
  public elle::serialization::VirtuallySerializable
{
  static constexpr char const* virtually_serializable_key = "type";

  virtual
  std::unique_ptr<infinit::model::Model>
  make() = 0;
};

namespace infinit
{
  namespace model {
class NodeModel: public infinit::model::Model
{
public:
  NodeModel(std::unique_ptr<infinit::model::doughnut::Local> local)
  : _local(std::move(local))
  {}
protected:
  virtual
  std::unique_ptr<blocks::MutableBlock>
  _make_mutable_block() const {return {};};
  virtual
  void
  _store(blocks::Block& block, StoreMode mode) {};
  virtual
  std::unique_ptr<blocks::Block>
  _fetch(Address address) const { return {};}
  virtual
  void
  _remove(Address address) {}
  std::unique_ptr<doughnut::Local> _local;
};
}}

struct DoughnutNodeModelConfig:
  public ModelConfig
{
public:
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  int port;
  DoughnutNodeModelConfig(elle::serialization::SerializerIn& input)
    : ModelConfig()
  {
    this->serialize(input);
  }
  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("storage", this->storage);
    s.serialize("port", this->port);
  }
  virtual
  std::unique_ptr<infinit::model::Model>
  make()
  {
    using namespace infinit::model::doughnut;
    std::unique_ptr<infinit::storage::Storage> store = this->storage->make();
    auto local = elle::make_unique<Local>(std::move(store), port);
    return std::unique_ptr<infinit::model::Model>(
      new infinit::model::NodeModel(std::move(local)));
  }
};

static const elle::serialization::Hierarchy<ModelConfig>::
Register<DoughnutNodeModelConfig> _register_DoughnutNodeModelConfig("doughnut_node");


struct DoughnutModelConfig:
  public ModelConfig
{
public:
  std::unique_ptr<OverlayConfig> overlay;
  std::unique_ptr<infinit::cryptography::KeyPair> key;
  boost::optional<bool> plain;;
  boost::optional<int> read_n;
  boost::optional<int> write_n;

  DoughnutModelConfig(elle::serialization::SerializerIn& input)
    : ModelConfig()
  {
    this->serialize(input);
  }
  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("overlay", this->overlay);
    s.serialize("key", this->key);
    s.serialize("plain", this->plain);
    s.serialize("read_n", this->read_n);
    s.serialize("write_n", this->write_n);
  }
  virtual
  std::unique_ptr<infinit::model::Model>
  make()
  {
    if (!key)
      return elle::make_unique<infinit::model::doughnut::Doughnut>(
        infinit::cryptography::KeyPair::generate(
          infinit::cryptography::Cryptosystem::rsa, 2048),
        overlay->make(),
        plain && *plain,
        write_n ? *write_n : 1,
        read_n ? *read_n : 1);
    else
      return elle::make_unique<infinit::model::doughnut::Doughnut>(
        std::move(*key),
        overlay->make(),
        plain && *plain,
        write_n ? *write_n : 1,
        read_n ? *read_n : 1);
  }
};

static const elle::serialization::Hierarchy<ModelConfig>::
Register<DoughnutModelConfig> _register_DoughnutModelConfig("doughnut");


struct FaithModelConfig:
  public ModelConfig
{
public:
  std::unique_ptr<infinit::storage::StorageConfig> storage;

  FaithModelConfig(elle::serialization::SerializerIn& input)
    : ModelConfig()
  {
    this->serialize(input);
  }

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("storage", this->storage);
  }

  virtual
  std::unique_ptr<infinit::model::Model>
  make()
  {
    return elle::make_unique<infinit::model::faith::Faith>
      (this->storage->make());
  }
};
static const elle::serialization::Hierarchy<ModelConfig>::
Register<FaithModelConfig> _register_FaithModelConfig("faith");

struct ParanoidModelConfig:
  public ModelConfig
{
public:
  // boost::optional does not support in-place construction, use a
  // std::unique_ptr instead since KeyPair is not copiable.
  std::unique_ptr<infinit::cryptography::KeyPair> keys;
  std::unique_ptr<infinit::storage::StorageConfig> storage;

  ParanoidModelConfig(elle::serialization::SerializerIn& input)
    : ModelConfig()
  {
    this->serialize(input);
  }

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("keys", this->keys);
    s.serialize("storage", this->storage);
  }

  virtual
  std::unique_ptr<infinit::model::Model>
  make()
  {
    if (!this->keys)
    {
      this->keys.reset(
        new infinit::cryptography::KeyPair(
          infinit::cryptography::KeyPair::generate
          (infinit::cryptography::Cryptosystem::rsa, 2048)));
      elle::serialization::json::SerializerOut output(std::cout);
      std::cout << "No key specified, generating fresh ones:" << std::endl;
      this->keys->serialize(output);
    }
    return elle::make_unique<infinit::model::paranoid::Paranoid>
      (std::move(*this->keys), this->storage->make());
  }
};
static const elle::serialization::Hierarchy<ModelConfig>::
Register<ParanoidModelConfig> _register_ParanoidModelConfig("paranoid");
>>>>>>> kademlia: first throw.

/*--------------.
| Configuration |
`--------------*/

struct Config
{
public:
  std::string mountpoint;
  boost::optional<elle::Buffer> root_address;
  std::shared_ptr<infinit::model::ModelConfig> model;
  boost::optional<bool> single_mount;

  Config()
    : mountpoint()
    , model()
  {}

  Config(elle::serialization::SerializerIn& input)
    : Config()
  {
    this->serialize(input);
  }

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("mountpoint", this->mountpoint);
    s.serialize("root_address", this->root_address);
    s.serialize("model", this->model);
    s.serialize("caching", this->single_mount);
  }
};

static
void
parse_options(int argc, char** argv, Config& cfg,
              std::unique_ptr<infinit::model::ModelConfig>& model_config)
{
  ELLE_TRACE_SCOPE("parse command line");
  using namespace boost::program_options;
  options_description options("Options");
  options.add_options()
    ("config,c", value<std::string>(), "configuration file")
    ("help,h", "display the help")
    ("version,v", "display version")
    ("model,m", "Only load model, do not mount any filesystem")
    ("rootfile,f", value<std::string>(), "File to store root address into")
    ;
  variables_map vm;
  try
  {
    store(parse_command_line(argc, argv, options), vm);
    notify(vm);
  }
  catch (invalid_command_line_syntax const& e)
  {
    throw elle::Error(elle::sprintf("command line error: %s", e.what()));
  }
  if (vm.count("help"))
  {
    std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << options;
    std::cout << std::endl;
    std::cout << "Infinit v" << INFINIT_VERSION << std::endl;
    exit(0); // XXX: use Exit exception
  }
  if (vm.count("version"))
  {
    std::cout << INFINIT_VERSION << std::endl;
    exit(0); // XXX: use Exit exception
  }
  if (vm.count("config") != 0)
  {
    std::string const config = vm["config"].as<std::string>();
    boost::filesystem::ifstream input_file(config);
    try
    {
      if (vm.count("model"))
      {
        elle::serialization::json::SerializerIn input(input_file, false);
        input.serialize_forward(model_config);
      }
      else
      {
        elle::serialization::json::SerializerIn input(input_file, false);
        cfg.serialize(input);
      }
    }
    catch (elle ::serialization::Error const& e)
    {
      throw elle::Error(
        elle::sprintf("error in configuration file %s: %s", config, e.what()));
    }
  }
  else
    throw elle::Error("missing mandatory 'config' option");
  if (vm.count("rootfile") != 0)
  {
    root_address_file = vm["rootfile"].as<std::string>();
  }
}

int
main(int argc, char** argv)
{
  try
  {
    reactor::Scheduler sched;
    reactor::Thread main(
      sched,
      "main",
      [argc, argv]
      {
        Config cfg;
        std::unique_ptr<infinit::model::ModelConfig> model_cfg;
        std::unique_ptr<infinit::model::Model> model;
        std::unique_ptr<infinit::model::Model> model2;
        parse_options(argc, argv, cfg, model_cfg);
        if (model_cfg)
        {
          model = model_cfg->make({}, true, true);
        }
        else
        {
          if (!cfg.model)
            throw elle::Error("missing mandatory \"model\" configuration key");
          model = cfg.model->make({}, true, true);
          std::unique_ptr<infinit::filesystem::FileSystem> fs;
          ELLE_TRACE("initialize filesystem")
            if (cfg.root_address)
            {
              using infinit::model::Address;
              auto root = elle::serialization::Serialize<Address>::
                convert(cfg.root_address.get());
              fs = elle::make_unique<infinit::filesystem::FileSystem>
                (root, std::move(model));
            }
            else
            {
              fs = elle::make_unique<infinit::filesystem::FileSystem>(
                std::move(model));
              std::cout << "No root block specified, generating fresh one:"
                        << std::endl;
              std::stringstream ss;
              {
                elle::serialization::json::SerializerOut output(ss);
                output.serialize_forward(fs->root_address());
              }
              std::cout << ss.str();
              if (root_address_file)
              {
                std::ofstream ofs(*root_address_file);
                ofs << ss.str().substr(1, ss.str().size()-3);
              }
            }
          if (cfg.single_mount && *cfg.single_mount)
            fs->single_mount(true);
          ELLE_TRACE("mount filesystem")
          {
            reactor::filesystem::FileSystem filesystem(std::move(fs), true);
            filesystem.mount(cfg.mountpoint, {});
            reactor::scheduler().signal_handle(SIGINT, [&] { filesystem.unmount();});
            ELLE_TRACE("Waiting on filesystem");
            reactor::wait(filesystem);
            ELLE_TRACE("Filesystem finished.");
          }
        }
      });
    sched.run();
  }
  catch (std::exception const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    return 1;
  }
}
