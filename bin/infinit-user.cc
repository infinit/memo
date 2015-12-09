#include <elle/log.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/pem.hh>

ELLE_LOG_COMPONENT("infinit-user");

#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

static std::string _hub_salt = "@a.Fl$4'x!";

using namespace boost::program_options;

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
get_name(variables_map const& args, std::string const& name = "name")
{
  return get_username(args, name);
}

void
upload_avatar(infinit::User& self,
              boost::filesystem::path const& avatar_path);
void
fetch_avatar(std::string const& name);
void
pull_avatar(infinit::User& self);
boost::optional<boost::filesystem::path>
avatar_path(std::string const& name);

COMMAND(export_)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  auto output = get_output(args);
  auto avatar = avatar_path(name);
  if (avatar)
    user.avatar_path = avatar.get().string();
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
  auto user_names =
    mandatory<std::vector<std::string>>(args, "name", "user name");
  for (auto const& name: user_names)
  {
    auto avatar = [&] () {
      if (!flag(args, "no-avatar"))
      {
        try
        {
          fetch_avatar(name);
        }
        catch (Redirected)
        {
        }
        catch (MissingResource)
        {
        }
      }
    };
    try
    {
      auto user = beyond_fetch<infinit::User>("user", name);
      ifnt.user_save(std::move(user));
      avatar();
    }
    catch (ResourceAlreadyFetched const& e)
    {
      avatar();
      throw;
    }
  }
}

std::string
hub_password_hash(variables_map const& args)
{
  return hash_password(_password(args, "password"),
                       _hub_salt);
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
  auto avatar_path = optional(args, "avatar");
  if (avatar_path && avatar_path.get().length() > 0)
  {
    if (!boost::filesystem::exists(avatar_path.get()))
      throw CommandLineError(
        elle::sprintf("%s doesn't exist", avatar_path.get()));
  }
  auto fullname = optional(args, "fullname");
  if (fullname) // Overwrite existing fullname.
  {
    user.fullname = fullname;
    user_updated = true;
  }
  if (flag(args, "full"))
  {
    user.password_hash = hub_password_hash(args);
    das::Serializer<infinit::DasPrivateUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
  else
  {
    if (args.count("password"))
    {
      throw CommandLineError(
        "Password is only used when pushing a full user");
    }
    das::Serializer<infinit::DasPublicUserPublish> view{user};
    beyond_push("user", user.name, view, user);
  }
  if (user_updated && !atomic)
    ifnt.user_save(user, true);
  if (avatar_path)
  {
    if (avatar_path.get().length() > 0)
      upload_avatar(user, avatar_path.get());
    else
      pull_avatar(user);
  }
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
      auto passphrase = read_passphrase("Key passphrase");
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
  bool push = aliased_flag(args, {"push-user", "push"});
  if (!push)
  {
    if (flag(args, "full") || flag(args, "password"))
    {
      throw CommandLineError(
        elle::sprintf("--full and --password are only used when pushing "
                      "a user to %s", beyond(true)));
    }
  }
  auto name = get_name(args);
  infinit::User user = create_(name,
                               optional(args, "key"),
                               optional(args, "email"),
                               optional(args, "fullname"));
  ifnt.user_save(user);
  report_action("generated", "user", name, std::string("locally"));
  if (push)
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
  if (!user.private_key)
    throw elle::Error(elle::sprintf(NO_PRIVATE_KEY, name));
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
    read_error<BeyondError>(r, "login", name);
  }

  return elle::json::read(r);
}

COMMAND(login)
{
  auto name = get_name(args);
  LoginCredentials c{name, hub_password_hash(args)};
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

template <typename Buffer>
void
_save_avatar(std::string const& name,
             Buffer const& buffer)
{
  boost::filesystem::ofstream f;
  ifnt._open_write(f, ifnt._user_avatar_path(name),
                   name, "avatar", true);
  f << buffer.string();
  report_action("fetched", "avatar", name, std::string("locally"));
}

void
upload_avatar(infinit::User& self,
              boost::filesystem::path const& avatar_path)
{
  boost::filesystem::ifstream icon;
  ifnt._open_read(icon, avatar_path, self.name, "icon");
  std::string s(
    std::istreambuf_iterator<char>{icon},
    std::istreambuf_iterator<char>{});
  elle::ConstWeakBuffer data(s.data(), s.size());
  auto url = elle::sprintf("users/%s/avatar", self.name);
  beyond_push_data(url, "avatar", self.name, data, "image/jpeg", self);
  _save_avatar(self.name, data);
}

void
fetch_avatar(std::string const& name)
{
  auto url = elle::sprintf("users/%s/avatar", name);
  auto redirect = beyond_fetch<FakeRedirect>(url, "avatar route", name);
  if (redirect.url)
  {
    auto response = fetch_data(redirect.url.get(), "avatar", name)->response();
    // XXX: Deserialize XML.
    if (response.size() == 0 || response[0] == '<')
      throw MissingResource(
        elle::sprintf("avatar for %s not found on %s", name, beyond(true)));
    _save_avatar(name, response);
  }
}

void
pull_avatar(infinit::User& self)
{
  auto url = elle::sprintf("users/%s/avatar", self.name);
  beyond_delete(url, "avatar", self.name, self);
}

boost::optional<boost::filesystem::path>
avatar_path(std::string const& name)
{
  auto path = ifnt._user_avatar_path(name);
  if (!boost::filesystem::exists(path))
    return boost::optional<boost::filesystem::path>{};
  return path;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  boost::program_options::option_description option_push_full =
    { "full", bool_switch(), "include private key in order "
      "to facilitate device pairing and fetching lost keys" };
  boost::program_options::option_description option_push_password =
    { "password", value<std::string>(), elle::sprintf(
      "password to authenticate with %s. Used with --full "
      "(default: prompt for password)", beyond(true)).c_str() };
  boost::program_options::option_description option_fullname =
    { "fullname", value<std::string>(), "user's fullname (optional)" };
  boost::program_options::option_description option_avatar =
    { "avatar", value<std::string>(), "path to an image to use as avatar" };

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
        option_fullname,
        option_push_full,
        option_push_password,
      },
    },
    {
      "export",
      "Export a user so that it may be imported elsewhere",
      &export_,
      {},
      {
        { "name,n", value<std::string>(),
          "user to export (default: system user)" },
        { "full", bool_switch(), "include private key "
          "(do not use this unless you understand the implications)" },
        option_output("user"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a user from %s", beyond(true)).c_str(),
      &fetch,
      {},
      {
        { "name,n", value<std::vector<std::string>>(), "user to fetch" },
        { "no-avatar", bool_switch(), "do not fetch user avatar" },
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
        option_fullname,
        option_avatar,
        option_push_full,
        option_push_password,
      },
    },
    {
      "signup",
      elle::sprintf("Create and push a user to %s", beyond(true)).c_str(),
      &signup_,
      {},
      {
        { "name,n", value<std::string>(), "user name (default: system user)" },
        { "email,n", value<std::string>(), "valid email address" },
        option_fullname,
        option_avatar,
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
        { "password", value<std::string>(), elle::sprintf(
          "password to authenticate with %s (default: prompt) for password",
          beyond(true)).c_str() },
      },
    },
    {
      "list",
      "List users",
      &list,
    },
  };
  return infinit::main("Infinit user utility", modes, argc, argv, {});
}
