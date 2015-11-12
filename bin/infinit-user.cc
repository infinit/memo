#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/pem.hh>

ELLE_LOG_COMPONENT("infinit-user");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

template <typename Super>
struct UserView
  : Super
{
  template <typename ... Args>
  UserView(Args&& ... args)
    : Super(std::forward<Args>(args)...)
  {}

  void
  serialize(elle::serialization::Serializer& s)
  {
    Super::serialize(s);
    std::string id(infinit::User::uid(this->object().public_key));
    s.serialize("id", id);
  }
};

inline
std::string
get_name(boost::program_options::variables_map const& args)
{
  return get_username(args, "name");
}

static
void
export_(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  auto output = get_output(args);
  if (args.count("full") && args["full"].as<bool>())
  {
    if (!script_mode)
    {
      elle::fprintf(std::cerr, "WARNING: you are exporting the user \"%s\" "
                    "including the private key\n", name);
      elle::fprintf(std::cerr, "WARNING: anyone in possession of this "
                    "information can impersonate that user\n");
      elle::fprintf(std::cerr, "WARNING: if you mean to export your user for "
                    "someone else, remove the --full flag\n");
    }
    UserView<das::Serializer<infinit::DasUser>> view(user);
    elle::serialization::json::serialize(view, *output, false);
  }
  else
  {
    UserView<das::Serializer<infinit::DasPublicUser>> view(user);
    elle::serialization::json::serialize(view, *output, false);
  }
  report_exported(*output, "user", user.name);
}

static
void
fetch(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto user_name = mandatory(args, "name", "user name");
  auto user =
    beyond_fetch<infinit::User>("user", user_name);
  ifnt.user_save(std::move(user));
}

void
echo_mode(bool enable)
{
#if defined(INFINIT_WINDOWS)
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(hStdin, &mode);
  if (!enable)
    mode &= ~ENABLE_ECHO_INPUT;
  else
    mode |= ENABLE_ECHO_INPUT;
  SetConsoleMode(hStdin, mode );
#else
  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);
  if(!enable)
    tty.c_lflag &= ~ECHO;
  else
    tty.c_lflag |= ECHO;
  (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}

std::string
read_passphrase()
{
  std::string res;
  {
    elle::SafeFinally restore_echo([] { echo_mode(true); });
    echo_mode(false);
    std::cout << "Passphrase: ";
    std::cout.flush();
    std::getline(std::cin, res);
  }
  std::cout << std::endl;
  return res;
}

std::string
hub_password(variables_map const& args)
{
  auto hash_password = [] (std::string const& password)
    {
      // XXX: Implent that.
      return password;
    };
  auto password = optional(args, "password");
  if (!password)
  {
    password = read_passphrase();
  }
  ELLE_ASSERT(password);
  return hash_password(password.get());
}

static
infinit::User
create_(std::string const& name,
        boost::optional<std::string> const& keys_file)
{
  auto keys = [&] // -> infinit::cryptography::rsa::KeyPair
  {
    if (keys_file)
    {
      auto passphrase = read_passphrase();
      return infinit::cryptography::rsa::pem::import_keypair(
          keys_file.get(), passphrase);
    }
    else
    {
      report("generating RSA keypair");
      return infinit::cryptography::rsa::keypair::generate(2048);
    }
  }();

  return infinit::User{name, keys};
}

static
void
create(variables_map const& args)
{
  auto name = get_name(args);
  auto keys_file = optional(args, "key");
  infinit::User user = create_(name, keys_file);
  ifnt.user_save(user);
  report_action("generated", "user", name, std::string("locally"));
}

static
void
import(variables_map const& args)
{
  auto input = get_input(args);
  {
    auto user =
      elle::serialization::json::deserialize<infinit::User>(*input, false);
    ifnt.user_save(user);
    report_imported("user", user.name);
  }
}

static
void
_push(variables_map const& args,
      infinit::User& user)
{
  auto email = mandatory(args, "email");
  user.email = email;
  if (args.count("full") && args["full"].as<bool>())
  {
    user.password_hash = hub_password(args);
    das::Serializer<infinit::DasPrivateUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
  else
  {
    das::Serializer<infinit::DasPublicUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
}

static
void
push(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  _push(args, user);
}

static
void
pull(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  beyond_delete("user", user.name, user);
}

static
void
delete_(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  auto path = ifnt._user_path(user.name);
  bool ok = boost::filesystem::remove(path);
  if (ok)
    report_action("deleted", "user", user.name, std::string("locally"));
  else
  {
    throw elle::Error(
        elle::sprintf("File for user could not be deleted: %s", path));
  }
}

static
void
signup_(variables_map const& args)
{
  auto name = get_name(args);
  auto email = mandatory(args, "email");
  auto keys_file = optional(args, "key");
  infinit::User user = create_(name, keys_file);
  try
  {
    ifnt.user_get(name);
  }
  catch (elle::Error const&)
  {
    _push(args, user);
    ifnt.user_save(user);
    return;
  }
  throw elle::Error(elle::sprintf("User %s already exists locally", name));
}

struct LoginCredentials
{
  LoginCredentials(std::string const& name,
                   std::string const& password)
    : name(name)
    , password_hash(password)
  {
  }

  LoginCredentials(elle::serialization::SerializerIn& s)
    : name(s.deserialize<std::string>("name"))
    , password_hash(s.deserialize<std::string>("password_hash"))
  {
  }

  std::string name;
  std::string password_hash;
};

DAS_MODEL(LoginCredentials, (name, password_hash), DasLoginCredentials)

template <typename T>
elle::json::Json
beyond_login(std::string const& name,
             T const& o)
{
  reactor::http::Request::Configuration c;
  c.header_add("Content-Type", "application/json");
  std::stringstream payload;
  elle::serialization::json::serialize(o, payload, false);
  reactor::http::Request r(elle::sprintf("%s/users/%s/login", beyond(), name),
                           reactor::http::Method::POST, std::move(c));
  r << payload.str();
  r.finalize();
  reactor::wait(r);
  if (r.status() == reactor::http::StatusCode::OK)
  {
  }
  else
  {
    elle::serialization::json::SerializerIn s(r, false);
    auto error = s.deserialize<BeyondError>();
    if (r.status() == reactor::http::StatusCode::Not_Found)
    {
      throw elle::Error(elle::sprintf("%s", error));
    }
    else if (r.status() == reactor::http::StatusCode::Forbidden)
    {
      throw elle::Error(elle::sprintf("%s", error));
    }
    throw;
  }
  return elle::json::read(r);
}

static
void
login(variables_map const& args)
{
  auto name = get_name(args);
  LoginCredentials c{name, hub_password(args)};
  das::Serializer<DasLoginCredentials> credentials{c};
  auto json = beyond_login(name, credentials);
  elle::serialization::json::SerializerIn input(json, false);
  auto user = input.deserialize<infinit::User>();
  ifnt.user_save(user);
  report_action("saved", "user", name, std::string("locally"));
}

static
void
list(variables_map const& args)
{
  for (auto const& user: ifnt.users_get())
    std::cout << user.name << std::endl;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  Modes modes {
    {
      "create",
      "Create a user",
      &create,
      {},
      {
        { "name,n", value<std::string>(),
          "user name (default: system user)" },
        { "key,k", value<std::string>(),
          "RSA key pair in PEM format - e.g. your SSH key "
          "(generated if unspecified)" },
      },
    },
    {
      "export",
      "Export a user for someone else to import",
      &export_,
      {},
      {
        { "name,n", value<std::string>(),
          "user to export (default: system user)" },
        { "full", bool_switch(),
          "include private information "
          "(do not use this unless you understand the implications)" },
        option_output("user"),
      },
    },
    {
      "fetch",
      "Fetch a user",
      &fetch,
      "--name USER",
      {
        { "name,n", value<std::string>(), "user to fetch" },
        option_owner,
      },
    },
    {
      "import",
      "Import a user",
      &import,
      {},
      {
        option_input("user"),
      },
    },
    {
      "pull",
      elle::sprintf("Remove a user from %s", beyond(true)).c_str(),
      &pull,
      {},
      {
        { "name,n", value<std::string>(),
          "user to push (default: system user)" },
      },
    },
    {
      "delete",
      "Delete a user",
      &delete_,
      {},
      {
        { "name,n", value<std::string>(),
          "user to delete (default: system user)" },
      },
    },
    {
      "push",
      elle::sprintf("Push a user to %s", beyond(true)).c_str(),
      &push,
      {},
      {
        { "name,n", value<std::string>(),
          "user to push (default: system user)" },
        { "email,n", value<std::string>(),
          "valid email address" },
        { "password", value<std::string>(),
          "password to authenticate to the hub" },
        {
          "full", bool_switch(),
          "push the whole user, including private information in order to "
          "facilitate device pairing. This information will be encrypted."}
      },
    },
    {
      "register",
      elle::sprintf("Push user to %s (alias for --push)", beyond(true)).c_str(),
      &push,
      {},
      {
        { "name,n", value<std::string>(),
          "user to register (default: system user)" },
      },
    },
    {
      "signup",
      "Create and register a user",
      &signup_,
      {},
      {
        { "name,n", value<std::string>(),
          "user name (default: system user)" },
        { "email,n", value<std::string>(),
          "valid email address" },
        { "key,k", value<std::string>(),
          "RSA key pair in PEM format - e.g. your SSH key"
            " (generated if unspecified)" },
        { "password", value<std::string>(),
          "password to authenticate to the hub" },
        { "full", bool_switch(),
          "push the whole user, including private information in order to "
          "facilitate device pairing. This information will be encrypted." },
      },
    },
    {
      "login",
      "Log the user to the hub",
      &login,
      {},
      {
        { "name,n", value<std::string>(),
          "user name (default: system user)" },
        { "password", value<std::string>(),
          "password to authenticate to the hub" },
      },
    },
    {
      "list",
      "List users",
      &list,
    },
  };
  return infinit::main("Infinit user utility", modes, argc, argv);
}
