#include <boost/program_options.hpp>

#include <elle/Error.hh>
#include <elle/Exit.hh>
#include <elle/cast.hh>
#include <elle/system/home_directory.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/version.hh>

std::string program;

namespace infinit
{
  int
  main(boost::program_options::options_description options,
       std::function<void (boost::program_options::variables_map,
                           std::vector<std::string>)> main,
       int argc,
       char** argv)
  {
    try
    {
      reactor::Scheduler sched;
      reactor::Thread main_thread(
        sched,
        "main",
        [&options, &main, &sched, argc, argv]
        {
          reactor::scheduler().signal_handle(SIGINT, [&] { sched.terminate();});
          ELLE_TRACE("parse command line")
          {
            using namespace boost::program_options;
            options_description misc("Miscellaneous");
            misc.add_options()
              ("help,h", "display the help")
              ("version,v", "display version")
            ;
            options.add(misc);
            variables_map vm;
            try
            {
              auto parser = command_line_parser(argc, argv);
              parser.options(options);
              parser.allow_unregistered();
              auto parsed = parser.run();
              store(parsed, vm);
              notify(vm);
              if (vm.count("version"))
              {
                std::cout << INFINIT_VERSION << std::endl;
                throw elle::Exit(0);
              }
              main(std::move(vm),
                   collect_unrecognized(parsed.options, include_positional));
            }
            catch (invalid_command_line_syntax const& e)
            {
              throw elle::Error(elle::sprintf("command line error: %s", e.what()));
            }
          }
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
    return 0;
  }

  struct Network
  {
    Network()
    {}

    Network(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("storage", this->storage);
      s.serialize("model", this->model);
      s.serialize("port", this->port);
    }

    std::unique_ptr<infinit::model::doughnut::Local>
    run()
    {
      auto local =
        elle::make_unique<infinit::model::doughnut::Local>
        (this->storage->make(), this->port);
      auto& dht = static_cast<infinit::model::doughnut::DoughnutModelConfig&>
        (*this->model);
      local->doughnut() = dht.make_read_only();
      return local;
    }

    std::unique_ptr<infinit::storage::StorageConfig> storage;
    std::unique_ptr<infinit::model::ModelConfig> model;
    int port;
  };

  struct Volume
  {
    Volume()
    {}

    Volume(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("mountpoint", this->mountpoint);
      s.serialize("root_address", this->root_address);
      s.serialize("network", this->network);
    }

    std::pair<
      std::unique_ptr<infinit::model::doughnut::Local>,
      std::unique_ptr<reactor::filesystem::FileSystem>>
    run()
    {
      auto local = this->network.run();
      auto fs = elle::make_unique<infinit::filesystem::FileSystem>
        (this->root_address, this->network.model->make());
      auto driver =
        elle::make_unique<reactor::filesystem::FileSystem>(std::move(fs), true);
      create_directories(boost::filesystem::path(mountpoint));
      driver->mount(mountpoint, {});
      return std::make_pair(std::move(local), std::move(driver));
    }

    std::string mountpoint;
    infinit::model::Address root_address;
    Network network;
  };

  class Infinit
  {
  public:
    Network
    network_get(std::string const& name)
    {
      boost::filesystem::ifstream f;
      this->_open(f, this->_network_path(name), name, "network");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<Network>();
    }

    void
    network_save(std::string const& name, Network const& network)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_network_path(name), name, "network");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(network);
    }

    boost::filesystem::path
    root_dir()
    {
      return elle::system::home_directory() / ".infinit";
    }

    std::unique_ptr<infinit::storage::StorageConfig>
    storage_get(std::string const& name)
    {
      boost::filesystem::ifstream f;
      this->_open(f, this->_storage_path(name), name, "storage");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<std::unique_ptr<infinit::storage::StorageConfig>>();
    }

    void
    storage_save(std::string const& name,
                 infinit::storage::StorageConfig const& storage)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_storage_path(name), name, "storage");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(storage);
    }

    void
    storage_remove(std::string const& name)
    {
      auto path = this->_storage_path(name);
      if (!remove(path))
        throw elle::Error(
          elle::sprintf("storage '%s' does not exist", name));
    }

    Volume
    volume_get(std::string const& name)
    {
      boost::filesystem::ifstream f;
      this->_open(f, this->_volume_path(name), name, "volume");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<Volume>();
    }

    void
    volume_save(std::string const& name, Volume const& volume)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_volume_path(name), name, "volume");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(volume);
    }

  private:
    boost::filesystem::path
    _network_path(std::string const& name)
    {
      auto root = this->root_dir() / "networks";
      create_directories(root);
      return root / name;
    }

    boost::filesystem::path
    _storage_path(std::string const& name)
    {
      auto root = this->root_dir() / "storage";
      create_directories(root);
      return root / name;
    }

    boost::filesystem::path
    _volume_path(std::string const& name)
    {
      auto root = this->root_dir() / "volumes";
      create_directories(root);
      return root / name;
    }

    void
    _open(boost::filesystem::ifstream& f,
          boost::filesystem::path const& path,
          std::string const& name,
          std::string const& type)
    {
      f.open(path);
      if (!f.good())
        throw elle::Error(elle::sprintf("%s '%s' does not exist", type, name));
    }

    void
    _open(boost::filesystem::ofstream& f,
          boost::filesystem::path const& path,
          std::string const& name,
          std::string const& type)
    {
      if (exists(path))
        throw elle::Error(
          elle::sprintf("%s '%s' already exists", type, name));
      f.open(path);
      if (!f.good())
        throw elle::Error(
          elle::sprintf("unable to open '%s' for writing", path));
    }
  };
}

class CommandLineSerializer
  : public elle::serialization::SerializerIn
{
public:
  typedef elle::serialization::SerializerIn Super;
  CommandLineSerializer(boost::program_options::variables_map const& vm)
    : Super(ELLE_SFINAE_INSTANCE(std::istream), false)
    , _variables(vm)
  {}

protected:
  boost::program_options::variables_map const& _variables;

  virtual
  void
  _serialize_array(std::string const& name,
                   int size, // -1 for in(), array size for out()
                   std::function<void ()> const& f)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, int64_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, uint64_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, int32_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, uint32_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, int8_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, uint8_t& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, double& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, bool& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, std::string& v)
  {
    v = this->_get(name).as<std::string>();
  }

  virtual
  void
  _serialize(std::string const& name, elle::Buffer& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize(std::string const& name, boost::posix_time::ptime& v)
  {
    ELLE_ABORT("not handled");
  }

  virtual
  void
  _serialize_option(std::string const& name,
                    bool present,
                    std::function<void ()> const& f)
  {
    ELLE_ABORT("not handled");
  }

private:
  boost::program_options::variable_value const&
  _get(std::string const& name)
  {
    auto count = this->_variables.count(name);
    if (!count)
      throw elle::Error(elle::sprintf("missing required '%s' option", name));
    if (count > 1)
      throw elle::Error(elle::sprintf("duplicate '%s' option", name));
    return this->_variables[name];
  }
};

std::string
mandatory(boost::program_options::variables_map const& vm,
          std::string const& name,
          std::string const& desc,
          std::function<void(std::ostream&)> const& help)
{
  if (!vm.count(name))
  {
    help(std::cerr);
    throw elle::Error(elle::sprintf("%s unspecified (use --%s)", desc, name));
  }
  return vm[name].as<std::string>();
}

boost::optional<std::string>
optional(boost::program_options::variables_map const& vm,
         std::string const& name)
{
  if (vm.count(name))
    return vm[name].as<std::string>();
  else
    return {};
}

std::string
mandatory(boost::program_options::variables_map const& vm,
          std::string const& name,
          std::function<void(std::ostream&)> const& help)
{
  return mandatory(vm, name, name, help);
}

boost::program_options::variables_map
parse_args(boost::program_options::options_description const& options,
           std::vector<std::string> const& args)
{
  auto parser = boost::program_options::command_line_parser(args);
  parser.options(options);
  boost::program_options::variables_map res;
  auto parsed = parser.run();
  boost::program_options::store(parsed, res);
  boost::program_options::notify(res);
  return res;
}
