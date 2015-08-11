#include <boost/program_options.hpp>

#include <elle/Error.hh>
#include <elle/Exit.hh>
#include <elle/cast.hh>
#include <elle/format/base64url.hh>
#include <elle/os/environ.hh>
#include <elle/system/home_directory.hh>
#include <elle/system/username.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <cryptography/hash.hh>
#include <cryptography/rsa/KeyPair.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/version.hh>

std::string program;

struct DropboxAccount
{
  int uid;
  std::string display_name;
  std::string token;
};

struct DropboxAccounts
{
  std::vector<DropboxAccount> dropbox_accounts;
};

DAS_MODEL(DropboxAccount, (uid, display_name, token), DasDropboxAccount);
DAS_MODEL_DEFAULT(DropboxAccount, DasDropboxAccount);
DAS_MODEL_SERIALIZE(DropboxAccount);
DAS_MODEL(DropboxAccounts, (dropbox_accounts), DasDropboxAccounts);
DAS_MODEL_DEFAULT(DropboxAccounts, DasDropboxAccounts);
DAS_MODEL_SERIALIZE(DropboxAccounts);

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

  struct User
  {
    User(std::string name_, cryptography::rsa::KeyPair keys_)
      : name(std::move(name_))
      , public_key(std::move(keys_.K()))
      , private_key(std::move(keys_.k()))
    {}

    User(elle::serialization::SerializerIn& s)
      : name(s.deserialize<std::string>("name"))
      , public_key(s.deserialize<cryptography::rsa::PublicKey>("public_key"))
      , private_key(s.deserialize<boost::optional<
                      cryptography::rsa::PrivateKey>>("private_key"))
    {}

    infinit::cryptography::rsa::KeyPair
    keypair() const
    {
      if (!this->private_key)
        throw elle::Error(
          elle::sprintf("user \"%s\" has no private key", this->name));
      return infinit::cryptography::rsa::KeyPair
        (this->public_key, this->private_key.get());
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("public_key", this->public_key);
      s.serialize("private_key", this->private_key);
    }

    std::string
    uid()
    {
      auto serial = elle::serialization::binary::serialize(this->public_key);
      auto hash = cryptography::hash(serial, cryptography::Oneway::sha256);
      return elle::format::base64url::encode(hash).string().substr(0, 8);
    }

    std::string name;
    cryptography::rsa::PublicKey public_key;
    boost::optional<cryptography::rsa::PrivateKey> private_key;
  };

  struct NetworkDescriptor
  {
    NetworkDescriptor(std::string name_,
                      std::unique_ptr<infinit::overlay::Configuration> overlay_,
                      infinit::cryptography::rsa::PublicKey owner_)
      : name(std::move(name_))
      , overlay(std::move(overlay_))
      , owner(std::move(owner_))
    {}

    NetworkDescriptor(elle::serialization::SerializerIn& s)
      : name(s.deserialize<std::string>("name"))
      , overlay(s.deserialize<std::unique_ptr<overlay::Configuration>>
                ("overlay"))
      , owner(s.deserialize<cryptography::rsa::PublicKey>("owner"))
    {
      this->serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("overlay", this->overlay);
      s.serialize("owner", this->owner);
    }

    std::string name;
    std::unique_ptr<infinit::overlay::Configuration> overlay;
    infinit::cryptography::rsa::PublicKey owner;
  };

  struct Network
  {
    Network()
      : name()
      , storage()
      , model()
      , port()
    {}

    Network(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("storage", this->storage);
      s.serialize("model", this->model);
      s.serialize("port", this->port);
    }

    std::pair<std::shared_ptr<infinit::model::doughnut::Local>,
              std::shared_ptr<infinit::model::doughnut::Doughnut>>
    run(std::vector<std::string> const& hosts = {},
        bool client = false)
    {
      auto dht_config =
        static_cast<model::doughnut::DoughnutModelConfig*>(this->model.get());
      if (this->storage)
      {
        auto local = std::make_shared<infinit::model::doughnut::Local>
          (this->storage->make(), this->port ? this->port.get() : 0);
        local->serve();
        return std::make_pair(std::move(local),
                              dht_config->make(hosts, client, local));
      }
      else
      {
        std::shared_ptr<infinit::model::doughnut::Local> local;
        return
          std::make_pair(local,
                         dht_config->make(hosts, client, local));
      }
    }

    std::string name;
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    std::unique_ptr<infinit::model::ModelConfig> model;
    boost::optional<int> port;
  };

  struct Volume
  {
    Volume(std::string name,
           boost::optional<std::string> mountpoint_,
           infinit::model::Address root_,
           std::string network_)
      : name(name)
      , mountpoint(std::move(mountpoint_))
      , root_address(std::move(root_))
      , network(std::move(network_))
    {}

    Volume(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("mountpoint", this->mountpoint);
      s.serialize("root_address", this->root_address);
      s.serialize("network", this->network);
    }

    std::unique_ptr<reactor::filesystem::FileSystem>
    run(std::shared_ptr<infinit::model::doughnut::Doughnut> dht,
        boost::optional<std::string> mountpoint_ = {})
    {
      auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
          this->root_address, dht);
      auto driver =
        elle::make_unique<reactor::filesystem::FileSystem>(std::move(fs), true);
      boost::filesystem::path mountpoint;
      if (mountpoint_)
        mountpoint = mountpoint_.get();
      else if (this->mountpoint)
        mountpoint = this->mountpoint.get();
      else
        throw elle::Error("mountpoint unspecified");
      try
      {
        create_directories(mountpoint);
      }
      catch (boost::filesystem::filesystem_error const&)
      {}
      driver->mount(mountpoint.string(), {});
      return driver;
    }

    std::string name;
    boost::optional<std::string> mountpoint;
    infinit::model::Address root_address;
    std::string network;
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

    NetworkDescriptor
    network_descriptor_get(std::string const& name)
    {
      boost::filesystem::ifstream f;
      this->_open(f, this->_network_path(name), name, "network");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<NetworkDescriptor>();
    }

    void
    network_save(std::string const& name, Network const& network)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_network_path(name), name, "network");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(network);
    }

    void
    network_save(std::string const& name, NetworkDescriptor const& network)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_network_path(name), name, "network");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(network);
    }

    void
    user_save(User const& user)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_user_path(user.name), user.name, "user");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(user);
    }

    User
    user_get(boost::optional<std::string> user = {})
    {
      if (!user)
        user = elle::system::username();
      boost::filesystem::ifstream f;
      this->_open(f, this->_user_path(user.get()), user.get(), "user");
      elle::serialization::json::SerializerIn s(f, false);
      return s.deserialize<User>();
    }

    boost::filesystem::path
    root_dir()
    {
      static auto const env  = elle::os::getenv("INFINIT_HOME", "");
      static auto const res =
        env.empty() ?
        elle::system::home_directory() / ".infinit" :
        boost::filesystem::path(env);
      return res;
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
    volume_save(Volume const& volume)
    {
      boost::filesystem::ofstream f;
      this->_open(f, this->_volume_path(volume.name), volume.name, "volume");
      elle::serialization::json::SerializerOut s(f, false);
      s.serialize_forward(volume);
    }

    void
    credentials_dropbox_add(DropboxAccount const& a)
    {
      auto path = this->_credentials_path(
        "dropbox", elle::sprintf("%s", a.uid));
      boost::filesystem::ofstream f(path);
      if (!f.good())
        throw elle::Error(
          elle::sprintf("unable to open %s for writing", path));
      elle::serialization::json::serialize(a, f, false);
    }

    std::vector<DropboxAccount>
    credentials_dropbox()
    {
      std::vector<DropboxAccount> res;
      auto const path = this->_credentials_path("dropbox");
      boost::filesystem::directory_iterator const end;
      for (boost::filesystem::directory_iterator it(path);
           it != end;
           ++it)
      {
        if (is_regular_file(it->status()))
        {
          boost::filesystem::ifstream f(it->path());
          if (!f.good())
            throw elle::Error(
              elle::sprintf("unable to open %s for reading", path));
          res.push_back(
            elle::serialization::json::deserialize<DropboxAccount>(f, false));
        }
      }
      return res;
    }

    DropboxAccount
    credentials_dropbox(std::string const& name)
    {
      for (auto const& account: this->credentials_dropbox())
        if (account.display_name == name
            || elle::sprintf("%s", account.uid) == name)
          return account;
      throw elle::Error(elle::sprintf("no such Dropbox account: %s", name));
    }

    boost::filesystem::path
    _credentials_path(std::string const& service)
    {
      auto root = this->root_dir() / "credentials" / service;
      create_directories(root);
      return root;
    }

    boost::filesystem::path
    _credentials_path(std::string const& service, std::string const& name)
    {
      return this->_credentials_path(service) / name;
    }

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
    _user_path(std::string const& name)
    {
      auto root = this->root_dir() / "users";
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
    if (this->_variables.count(name))
      f();
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

std::unique_ptr<std::ostream, std::function<void (std::ostream*)>>
get_output(boost::program_options::variables_map const& args)
{
  if (args.count("output"))
  {
    auto dest = args["output"].as<std::string>();
    if (dest != "-")
    {
      std::unique_ptr<std::ostream, std::function<void (std::ostream*)>> file
        (new std::ofstream(dest), [] (std::ostream* p) { delete p; });
      if (!file->good())
        throw elle::Error(
          elle::sprintf("unable to open \"%s\" for writing", dest));
      return file;
    }
  }
  return std::unique_ptr<std::ostream, std::function<void (std::ostream*)>>
    (&std::cout, [] (std::ostream*) {});
}

std::unique_ptr<std::istream, std::function<void (std::istream*)>>
get_input(boost::program_options::variables_map const& args)
{
  if (args.count("input"))
  {
    auto dest = args["input"].as<std::string>();
    if (dest != "-")
    {
      std::unique_ptr<std::istream, std::function<void (std::istream*)>> file
        (new std::ifstream(dest), [] (std::istream* p) { delete p; });
      if (!file->good())
        throw elle::Error(
          elle::sprintf("unable to open \"%s\" for reading", dest));
      return file;
    }
  }
  return std::unique_ptr<std::istream, std::function<void (std::istream*)>>
    (&std::cin, [] (std::istream*) {});
}

inline
std::string
get_username(boost::program_options::variables_map const& args,
             std::string const& name)
{
  auto opt = optional(args, name);
  return opt ? opt.get() : elle::system::username();
}

inline
std::string
get_name(boost::program_options::variables_map const& args)
{
  return get_username(args, "name");
}

DAS_MODEL_FIELDS(infinit::User, (name, public_key, private_key));

namespace infinit
{
  DAS_MODEL_DEFINE(User, (name, public_key, private_key), DasUser);
  DAS_MODEL_DEFINE(User, (name, public_key), DasPublicUser);
}

static auto const beyond = "http://127.0.0.1:8080";
