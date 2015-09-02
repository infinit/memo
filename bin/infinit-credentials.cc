#include <elle/log.hh>
#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

static
void
add(variables_map const& args)
{
  if (args.count("dropbox"))
  {
    auto user = ifnt.user_get(get_username(args, "user"));
    std::cout << "Register your Dropbox account with infinit by visiting "
              << beyond() << "/users/" << user.uid()
              << "/dropbox-oauth" << std::endl;
  }
  else if (args.count("google"))
  {
    auto user = ifnt.user_get(get_username(args, "user"));
    std::cout << "Register your Google account with infinit by visiting "
              << beyond() << "/users/" << user.uid()
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
  reactor::http::Request r(
    elle::sprintf("%s/users/%s/credentials/%s", beyond(), user.uid(), name),
    reactor::http::Method::GET);
  if (r.status() == reactor::http::StatusCode::Not_Found)
  {
    throw elle::Error(elle::sprintf("user %s is not published",
                                    user.name));
  }
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

static
void
fetch(variables_map const& args)
{
  auto e = enabled(args);
  auto user = ifnt.user_get(get_username(args, "user"));
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

static
void
list(variables_map const& args)
{
  auto e = enabled(args);
  list_(e, s::dropbox, s::credentials_dropbox);
  list_(e, s::google, s::credentials_google);
}

int
main(int argc, char** argv)
{
  option_description user(
    "user",
    value<std::string>(),
    "user to manage credentials with (defaults to system user)");
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
      {
        user,
      },
      {services_options},
    },
    {
      "fetch",
      "Synchronize credentials locally",
      &fetch,
      "[SERVICE]",
      {
        user,
      },
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
