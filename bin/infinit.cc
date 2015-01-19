#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>

#include <elle/Error.hh>
#include <elle/printf.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/model/Model.hh>
#include <infinit/storage/Async.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("infinit");

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


/*--------------------.
| Model configuration |
`--------------------*/

struct ModelConfig:
  public elle::serialization::VirtuallySerializable
{
  static constexpr char const* virtually_serializable_key = "type";

  virtual
  std::unique_ptr<infinit::model::Model>
  make() const = 0;
};

struct FaithModelConfig:
  public ModelConfig
{
public:
  std::shared_ptr<infinit::storage::StorageConfig> storage;

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
  make() const
  {
    return elle::make_unique<infinit::model::faith::Faith>
      (this->storage->make());
  }
};
static const elle::serialization::Hierarchy<ModelConfig>::
Register<FaithModelConfig> _register_FaithModelConfig("faith");

/*--------------.
| Configuration |
`--------------*/

struct Config
{
public:
  std::string mountpoint;
  boost::optional<std::string> root_address;
  std::shared_ptr<ModelConfig> model;

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
  }
};

static
void
parse_options(int argc, char** argv, Config& cfg)
{
  ELLE_TRACE_SCOPE("parse command line");
  using namespace boost::program_options;
  options_description options("Options");
  options.add_options()
    ("help,h", "display the help")
    ("config,c", value<std::string>(), "configuration file")
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
    exit(0); // XXX: use Exit exception
  }
  if (vm.count("config") != 0)
  {
    std::string const config = vm["config"].as<std::string>();
    boost::filesystem::ifstream input_file(config);
    try
    {
      elle::serialization::json::SerializerIn input(input_file);
      cfg.serialize(input);
    }
    catch (elle::serialization::Error const& e)
    {
      throw elle::Error(
        elle::sprintf("error in configuration file %s: %s", config, e.what()));
    }
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
        parse_options(argc, argv, cfg);
        auto model = cfg.model->make();
        std::unique_ptr<infinit::filesystem::FileSystem> fs;
        ELLE_TRACE("initialize filesystem")
          if (cfg.root_address)
          {
            fs = elle::make_unique<infinit::filesystem::FileSystem>(
              infinit::model::Address::from_string(cfg.root_address.get()),
              std::move(model));
          }
          else
            fs = elle::make_unique<infinit::filesystem::FileSystem>(
              std::move(model));
        ELLE_TRACE("mount filesystem")
        {
          reactor::filesystem::FileSystem filesystem(std::move(fs), true);
          filesystem.mount(cfg.mountpoint, {});
          reactor::scheduler().signal_handle(SIGINT, [&] { filesystem.unmount();});
          reactor::wait(filesystem);
        }
      });
    sched.run();
  }
  catch (std::exception const& e)
  {
    elle::fprintf(std::cerr, "fatal error: %s\n", e.what());
    return 1;
  }
}
