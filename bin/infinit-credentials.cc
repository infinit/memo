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
    std::cout << "Register your dropbox account with infinit by visiting "
              << beyond() << "/users/" << user.uid()
              << "/dropbox-oauth" << std::endl;
  }
  else
    throw CommandLineError("service type not specified");
}

static
void
fetch(variables_map const& args)
{
  // if (args.count("dropbox"))
  auto user = ifnt.user_get(get_username(args, "user"));
  reactor::http::Request r(
    elle::sprintf("%s/users/%s/dropbox-accounts", beyond(), user.uid()),
    reactor::http::Method::GET);
  if (r.status() == reactor::http::StatusCode::Not_Found)
  {
    throw elle::Error(elle::sprintf("user %s is not published",
                                    user.name));
  }
  auto accounts =
    elle::serialization::json::deserialize<DropboxAccounts>(r, false);
  for (auto const& a: accounts.dropbox_accounts)
  {
    elle::printf("Fetched Dropbox account %s (%s)\n",
                 a.uid, a.display_name);
    ifnt.credentials_dropbox_add(a);
  }
  // FIXME: remove deleted ones
}

static
void
list(variables_map const& args)
{
  if (!args.count("dropbox"))
    std::cout << "Dropbox:" << std::endl;
  for (auto const& account: ifnt.credentials_dropbox())
    std::cout << account.uid << ": " << account.display_name << std::endl;
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
