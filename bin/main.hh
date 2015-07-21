#include <boost/program_options.hpp>

#include <elle/Error.hh>
#include <elle/Exit.hh>
#include <elle/cast.hh>
#include <elle/system/home_directory.hh>

#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/version.hh>

namespace infinit
{
  int
  main(boost::program_options::options_description options,
       std::function<void (boost::program_options::variables_map)> main,
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
            boost::program_options::options_description misc("Miscellaneous");
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
              // parser.allow_unregistered();
              store(parser.run(), vm);
              notify(vm);
            }
            catch (invalid_command_line_syntax const& e)
            {
              throw elle::Error(elle::sprintf("command line error: %s", e.what()));
            }
            if (vm.count("version"))
            {
              std::cout << INFINIT_VERSION << std::endl;
              throw elle::Exit(0);
            }
            main(std::move(vm));
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
      auto dht = elle::cast<infinit::model::doughnut::DoughnutModelConfig>
        ::compiletime(this->model);
      dht->keys.reset();
      dht->name.reset();
      local->doughnut() =
        elle::cast<infinit::model::doughnut::Doughnut>::compiletime
        (dht->make());
      return local;
    }

    std::unique_ptr<infinit::storage::StorageConfig> storage;
    std::unique_ptr<infinit::model::ModelConfig> model;
    int port;
  };

  class Infinit
  {
  public:
    Network
    network(std::string const& name)
    {
      boost::filesystem::ifstream f;
      this->_open(f, this->_network_path(name), "network");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<Network>();
    }

    boost::filesystem::path
    root_dir()
    {
      return elle::system::home_directory() / ".infinit";
    }

  private:
    boost::filesystem::path
    _network_path(std::string const& name)
    {
      return this->root_dir() / "networks" / name;
    }

    void
    _open(boost::filesystem::ifstream& f,
          boost::filesystem::path const& path,
          std::string const& name)
    {
      f.open(path);
      if (!f.good())
        throw elle::Error(elle::sprintf("network '%s' does not exist", name));
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
