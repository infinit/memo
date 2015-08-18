#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <reactor/http/Request.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

#include <main.hh>

using namespace boost::program_options;
options_description mode_options("Modes");
options_description common_options("Options");
options_description services_options("Services");

infinit::Infinit ifnt;

void
credentials(boost::program_options::variables_map mode,
            std::vector<std::string> args)
{
  auto help = [&] (std::ostream& output)
    {
      if (mode.count("add"))
      {
        output << "Usage: " << program
        << " --add [OPTIONS] SERVICE"
        << std::endl;
      }
      else if (mode.count("fetch"))
      {
        output << "Usage: " << program
        << " --fetch [OPTIONS] [SERVICE]"
        << std::endl;
      }
      else
      {
        output << "Usage: " << program
        << " MODE [OPTIONS] [SERVICE]"
        << std::endl;
        output << std::endl;
        output << mode_options;
      }
      output << std::endl;
      output << common_options;
      output << std::endl;
      output << services_options;
      output << std::endl;
    };
  if (mode.count("help"))
  {
    help(std::cout);
    throw elle::Exit(0);
  }
  if (mode.count("add"))
  {
    if (mode.count("dropbox"))
    {
      auto user = ifnt.user_get(get_username(mode, "user"));
      std::cout << "Register your dropbox account with infinit by visiting "
                << beyond << "/users/" << user.uid()
                << "/dropbox-oauth" << std::endl;
    }
    else
    {
      help(std::cerr);
      throw elle::Error("service type not specified");
    }
  }
  else if (mode.count("fetch"))
  {
    if (mode.count("dropbox"))
    {
      auto user = ifnt.user_get(get_username(mode, "user"));
      reactor::http::Request r(
        elle::sprintf("%s/users/%s/dropbox-accounts", beyond, user.uid()),
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
        elle::printf("Fetched Dropbox account %s (%s)", a.uid, a.display_name);
        ifnt.credentials_dropbox_add(a);
      }
      // FIXME: remove deleted ones
    }
    else
    {
      help(std::cerr);
      throw elle::Error("service type not specified");
    }
  }
  else if (mode.count("list"))
  {
    if (mode.count("dropbox"))
    {
      for (auto const& account: ifnt.credentials_dropbox())
        std::cout << account.uid << ": " << account.display_name << std::endl;
    }
    else
    {
      help(std::cerr);
      throw elle::Error("service type not specified");
    }
  }
  else
  {
    std::cerr << "Usage: " << program << " [mode] [mode-options]" << std::endl;
    std::cerr << std::endl;
    std::cerr << mode_options;
    std::cerr << std::endl;
    throw elle::Error("mode unspecified");
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  mode_options.add_options()
    ("add", "add credentials to a service")
    ("fetch", "synchronize your credentials locally")
    ("list", "list local credentials")
    ("revoke", "revoke service credentials")
    ;
  common_options.add_options()
    ("user", "user to manage credentials with (defaults to system user)")
    ;
  services_options.add_options()
    ("dropbox", "Dropbox account credentials")
    ;
  options_description options("Infinit external credentials utility");
  options.add(mode_options);
  options.add(common_options);
  options.add(services_options);
  return infinit::main(options, &credentials, argc, argv);
}
