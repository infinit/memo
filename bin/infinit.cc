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
  std::string name;

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
          model = model_cfg->make();
        }
        else
        {
          if (!cfg.model)
            throw elle::Error("missing mandatory \"model\" configuration key");
          model = cfg.model->make();
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
