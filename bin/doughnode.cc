#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>

#include <elle/Exit.hh>
#include <elle/cast.hh>
#include <elle/serialization/json.hh>
#include <elle/Version.hh>

#include <reactor/scheduler.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("doughnode");

# define INFINIT_ELLE_VERSION elle::Version(INFINIT_MAJOR,   \
                                            INFINIT_MINOR,   \
                                            INFINIT_SUBMINOR)

struct Config
{
public:
  boost::optional<int> port;
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  std::shared_ptr<infinit::model::ModelConfig> model;

  Config()
    : port(0)
    , storage()
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
    s.serialize("port", this->port);
    s.serialize("storage", this->storage);
    s.serialize("model", this->model);
  }
};

static
void
parse_options(int argc, char** argv, Config& cfg, boost::filesystem::path & p)
{
  ELLE_TRACE_SCOPE("parse command line");
  using namespace boost::program_options;
  options_description options("Options");
  options.add_options()
    ("config,c", value<std::string>(), "configuration file")
    ("help,h", "display the help")
    ("version,v", "display version")
    ("force-version,f", value<std::string>(), "force used version")
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
  elle::Version version = vm.count("force-version")
    ? elle::Version::from_string(vm["force-version"].as<std::string>())
    : INFINIT_ELLE_VERSION;
  if (vm.count("help"))
  {
    std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << options;
    std::cout << std::endl;
    std::cout << "Infinit v" << version << std::endl;
    throw elle::Exit(0);
  }
  if (vm.count("version"))
  {
    std::cout << version << std::endl;
    throw elle::Exit(0);
  }
  if (vm.count("config") != 0)
  {
    std::string const config = vm["config"].as<std::string>();
    boost::filesystem::ifstream input_file(config);
    try
    {
      elle::serialization::json::SerializerIn input(input_file, false);
      input.serialize_forward(cfg);
    }
    catch (elle::Error const& e)
    {
      throw elle::Error(
        elle::sprintf("error in configuration file %s: %s", config, e.what()));
    }
    p = boost::filesystem::path(config).parent_path() / "cache";
  }
  else
    throw elle::Error("missing mandatory 'config' option");
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
        boost::filesystem::path p;
        parse_options(argc, argv, cfg, p);
        ELLE_ASSERT(cfg.model.get());
        auto model =
          cfg.model->make(infinit::overlay::NodeEndpoints(), false, p);
        reactor::sleep();
      });
    sched.run();
  }
  catch (elle::Exit const& e)
  {
    return e.return_code();
  }
  catch (std::exception const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    return 1;
  }
}
