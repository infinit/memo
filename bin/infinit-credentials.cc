#include <elle/log.hh>
#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

COMMAND(add)
{
  if (args.count("dropbox"))
  {
    auto user = self_user(ifnt, args);
    std::cout << "Register your Dropbox account with infinit by visiting "
              << beyond() << "/users/" << user.name
              << "/dropbox-oauth" << std::endl;
  }
  else if (args.count("google"))
  {
    auto user = ifnt.user_get(get_username(args, "user"));
    std::cout << "Register your Google account with infinit by visiting "
              << beyond() << "/users/" << user.name
              << "/google-oauth" << std::endl;
  }
  else
    throw CommandLineError("service type not specified");
}

struct Enabled
{
  bool dropbox;
  bool google;
  bool multi;
};

Enabled
enabled(variables_map const& args)
{
  int dropbox = args.count("dropbox") ? 1 : 0;
  int google = args.count("google") ? 1 : 0;
  if (!dropbox && !google)
    dropbox = google = 1;
  return Enabled { bool(dropbox), bool(google), dropbox + google > 1 };
}

void
fetch_credentials(infinit::User const& user,
                  std::string const& name,
                  std::string const& pretty,
                  std::function<void (Credentials)> add)
{
  using namespace infinit::cryptography;
  std::string where = elle::sprintf(
    "users/%s/credentials/%s",
    user.name, name);
  elle::Buffer string_to_sign("GET;", 4);
  string_to_sign.append(where.data(), where.size());
  string_to_sign.append(";", 1);
  auto payload_hash = hash(elle::ConstWeakBuffer(),
                           Oneway::sha256);
  auto pl64 = elle::format::base64::encode(payload_hash);
  string_to_sign.append(pl64.contents(), pl64.size());

  string_to_sign.append(";", 1);
  auto now = std::to_string(time(0));
  string_to_sign.append(now.data(), now.size());
  auto sig = user.private_key->sign(
    string_to_sign,
    infinit::cryptography::rsa::Padding::pkcs1,
    infinit::cryptography::Oneway::sha256);
  auto sig64 = elle::format::base64::encode(sig);
  reactor::http::Request::Configuration c;
  c.header_add("infinit-signature", sig64.string());
  c.header_add("infinit-time", now);
  reactor::http::Request r(
    elle::sprintf("%s/%s", beyond(), where),
    reactor::http::Method::GET, std::move(c));
  if (r.status() == reactor::http::StatusCode::Not_Found)
  {
    throw elle::Error(elle::sprintf("user %s is not published",
                                    user.name));
  }
  ELLE_DUMP("response: %s", r);
  auto credentials =
    elle::serialization::json::deserialize<std::vector<Credentials>>
    (r, "credentials", false);
  for (auto& a: credentials)
  {
    elle::printf("Fetched %s credentials %s (%s)\n",
                 pretty, a.uid, a.display_name);
    add(std::move(a));
  }
}

COMMAND(fetch)
{
  auto e = enabled(args);
  auto user = self_user(ifnt, args);
  if (e.dropbox)
    fetch_credentials(
      user, "dropbox", "Dropbox",
      [] (Credentials a) { ifnt.credentials_dropbox_add(std::move(a)); });
  if (e.google)
    fetch_credentials(
      user, "google", "Google Drive",
      [] (Credentials a) { ifnt.credentials_google_add(std::move(a)); });
  // FIXME: remove deleted ones
}

#define SYMBOL(Sym)                                                     \
namespace s                                                             \
{                                                                       \
  struct                                                                \
  {                                                                     \
    template <typename T>                                               \
    static                                                              \
    auto                                                                \
    get_attribute(T const& o)                                           \
      -> decltype(o.Sym)                                                \
    {                                                                   \
      return o.Sym;                                                     \
    }                                                                   \
                                                                        \
    template <typename T, typename ... Args>                            \
    static                                                              \
    auto                                                                \
    call_method(T const& o, Args&& ... args)                            \
      -> decltype(o.Sym(std::forward<Args>(args)...))                   \
    {                                                                   \
      return o.Sym(std::forward<Args>(args)...);                        \
    }                                                                   \
  } Sym;                                                                \
};                                                                      \

SYMBOL(dropbox);
SYMBOL(credentials_dropbox);
SYMBOL(google);
SYMBOL(credentials_google);

template <typename Service, typename Fetch>
void
list_(Enabled const& e, Service service, Fetch fetch)
{
  if (!service.get_attribute(e))
    return;
  bool first = true;
  for (auto const& credentials: fetch.call_method(ifnt))
  {
    if (e.multi && first)
      std::cout << "Dropbox:" << std::endl;
    if (e.multi)
      std::cout << "  ";
    std::cout << credentials.uid << ": "
              << credentials.display_name << std::endl;
    first = false;
  }
}

COMMAND(list)
{
  auto e = enabled(args);
  list_(e, s::dropbox, s::credentials_dropbox);
  list_(e, s::google, s::credentials_google);
}

int
main(int argc, char** argv)
{
  options_description services_options("Services");
  services_options.add_options()
    ("dropbox", "Dropbox account credentials")
    ("google", "Google account credentials")
    ;
  program = argv[0];
  Modes modes {
    {
      "add",
      "Add credentials for a third-party service",
      &add,
      "SERVICE",
      {},
      {services_options},
    },
    {
      "fetch",
      elle::sprintf("Fetch credentials from %s", beyond(true)),
      &fetch,
      "[SERVICE]",
      {},
      {services_options},
    },
    {
      "list",
      "List local credentials",
      &list,
      "[SERVICE]",
      {},
      {services_options},
    },
  };
  return infinit::main("Infinit third-party credentials utility",
                       modes, argc, argv);
}
