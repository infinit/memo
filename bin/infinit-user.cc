#include <elle/log.hh>
#include <elle/format/hexadecimal.hh>
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

COMMAND(export_)
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

COMMAND(fetch)
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
      return elle::format::hexadecimal::encode(
        infinit::cryptography::hash(
          password, infinit::cryptography::Oneway::sha256).string()
        );
      return password;
    };
  auto password = optional(args, "hub-password-inline");
  if (!password)
    password = read_passphrase();
  ELLE_ASSERT(password);
  return hash_password(password.get());
}

static
void
_push(variables_map const& args, infinit::User& user, bool atomic)
{
  auto email = optional(args, "email");
  bool user_updated = false;
  if (!user.email && !email)
  {
    throw CommandLineError(elle::sprintf(
      "users pushed to %s must have an email address (use --email)",
      beyond(true)));
  }
  if (email) // Overwrite existing email.
  {
    user.email = email;
    user_updated = true;
  }
  auto fullname = optional(args, "fullname");
  if (fullname) // Overwrite existing fullname.
  {
    user.fullname = fullname;
    user_updated = true;
  }
  if (flag(args, "full"))
  {
    user.password_hash = hub_password(args);
    das::Serializer<infinit::DasPrivateUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
  else
  {
    if (args.count("hub-password-inline"))
    {
      throw CommandLineError(
        "Hub password is only used when pushing a full user");
    }
    das::Serializer<infinit::DasPublicUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
  if (user_updated && !atomic)
    ifnt.user_save(user, true);
}

static
infinit::User
create_(std::string const& name,
        boost::optional<std::string> keys_file,
        boost::optional<std::string> email,
        boost::optional<std::string> fullname)
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

  return infinit::User{name, keys, email, fullname};
}

COMMAND(create)
{
  auto name = get_name(args);
  infinit::User user = create_(name,
                               optional(args, "key"),
                               optional(args, "email"),
                               optional(args, "fullname"));
  ifnt.user_save(user);
  report_action("generated", "user", name, std::string("locally"));
  if (aliased_flag(args, {"push-user", "push"}))
    _push(args, user, false);
}

COMMAND(import)
{
  auto input = get_input(args);
  {
    auto user =
      elle::serialization::json::deserialize<infinit::User>(*input, false);
    ifnt.user_save(user);
    report_imported("user", user.name);
  }
}

COMMAND(push)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  _push(args, user, false);
}

COMMAND(pull)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  beyond_delete("user", user.name, user);
}

COMMAND(delete_)
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

COMMAND(signup_)
{
  auto name = get_name(args);
  infinit::User user = create_(name,
                               optional(args, "key"),
                               mandatory(args, "email"),
                               optional(args, "fullname"));
  try
  {
    ifnt.user_get(name);
  }
  catch (elle::Error const&)
  {
    _push(args, user, true);
    ifnt.user_save(user, true);
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
  {}

  LoginCredentials(elle::serialization::SerializerIn& s)
    : name(s.deserialize<std::string>("name"))
    , password_hash(s.deserialize<std::string>("password_hash"))
  {}

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
  reactor::http::Request r(elle::sprintf("%s/users/%s/login", beyond(), name),
                           reactor::http::Method::POST, std::move(c));
  elle::serialization::json::serialize(o, r, false);
  r.finalize();
  if (r.status() != reactor::http::StatusCode::OK)
  {
    auto error = elle::serialization::json::deserialize<BeyondError>(r, false);
    throw elle::Error(elle::sprintf("%s", error));
  }
  else
    return elle::json::read(r);
}

COMMAND(login)
{
  auto name = get_name(args);
  LoginCredentials c{name, hub_password(args)};
  das::Serializer<DasLoginCredentials> credentials{c};
  auto json = beyond_login(name, credentials);
  elle::serialization::json::SerializerIn input(json, false);
  auto user = input.deserialize<infinit::User>();
  ifnt.user_save(user, true);
  report_action("saved", "user", name, std::string("locally"));
}

COMMAND(list)
{
  for (auto const& user: ifnt.users_get())
  {
    std::cout << user.name << ": public";
    if (user.private_key)
      std::cout << "/private keys";
    else
      std::cout << " key only";
    std::cout << std::endl;
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  boost::program_options::option_description option_push_full =
    { "full", bool_switch(), elle::sprintf("include private key in order "
      "to facilitate device pairing and fetching lost keys. "
      "Keys are encrypted on %s", beyond(true)).c_str() };
  boost::program_options::option_description option_push_password =
    { "hub-password-inline", value<std::string>(), elle::sprintf(
      "password to authenticate with %s. Use this option with --full "
      "to avoid password prompt", beyond(true)).c_str() };
  Modes modes {
    {
      "create",
      "Create a user",
      &create,
      {},
      {
        { "name,n", value<std::string>(), "user name (default: system user)" },
        { "key,k", value<std::string>(),
          "RSA key pair in PEM format - e.g. your SSH key "
          "(default: generate key pair)" },
        { "push-user", bool_switch(),
          elle::sprintf("push the user to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-user" },
        { "email", value<std::string>(),
          "valid email address (mandatory when using --push-user)" },
        { "fullname", value<std::string>(), "user's fullname (optional)" },
        option_push_full,
        option_push_password,
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
      elle::sprintf("Fetch a user from %s", beyond(true)).c_str(),
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
          "user to remove (default: system user)" },
      },
    },
    {
      "delete",
      "Delete a user locally",
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
        { "email", value<std::string>(), "valid email address" },
        option_push_full,
        option_push_password,
      },
    },
    {
      "signup",
      "Create and register a user",
      &signup_,
      {},
      {
        { "name,n", value<std::string>(), "user name (default: system user)" },
        { "email,n", value<std::string>(), "valid email address" },
        { "fullname", value<std::string>(), "user's fullname (optional)" },
        { "key,k", value<std::string>(),
          "RSA key pair in PEM format - e.g. your SSH key "
          "(default: generate key pair)" },
        option_push_full,
        option_push_password,
      },
    },
    {
      "login",
      elle::sprintf("Log the user to %s", beyond(true)).c_str(),
      &login,
      {},
      {
        { "name,n", value<std::string>(),
          "user name (default: system user)" },
        { "hub-password-inline", value<std::string>(), elle::sprintf(
          "password to authenticate with %s", beyond(true)).c_str() },
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
