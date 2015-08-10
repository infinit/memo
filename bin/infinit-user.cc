#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/username.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <reactor/http/Request.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/overlay/Stonehenge.hh>

ELLE_LOG_COMPONENT("8network");

#include "main.hh"

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

void
network(boost::program_options::variables_map mode,
        std::vector<std::string> args)
{
  if (mode.count("generate"))
  {
    options_description creation_options("Creation options");
    creation_options.add_options()
      ("name", value<std::string>(), "user name (defaults to system username)")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --generate [options]" << std::endl;
      output << std::endl;
      output << creation_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    variables_map creation = parse_args(creation_options, args);
    auto name = get_name(creation);
    infinit::User user(name,
                       infinit::cryptography::rsa::keypair::generate(2048));
    ifnt.user_save(user);
    elle::printf("Generated user %s.\n", name);
  }
  else if (mode.count("export"))
  {
    options_description export_options("Export options");
    export_options.add_options()
      ("name,n", value<std::string>(), "user to export (defaults to system user)")
      ("full", "also export private information "
               "(do not use this unless you understand the implications)")
      ("output,o", value<std::string>(), "file to write user to "
                                        "(stdout by default)")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --export [options]" << std::endl;
      output << std::endl;
      output << export_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto exportation = parse_args(export_options, args);
    auto name = get_name(exportation);
    auto user = ifnt.user_get(name);
    auto output = get_output(exportation);
    if (exportation.count("full"))
    {
      elle::fprintf(std::cerr, "WARNING: you are exporting the user \"%s\" "
                    "including the private key\n", name);
      elle::fprintf(std::cerr, "WARNING: anyone in possession of this "
                    "information can impersonate that user\n");
      elle::fprintf(std::cerr, "WARNING: if you mean to export your user for "
                    "someone else, remove the --full flag\n");
      elle::serialization::json::SerializerOut s(std::cout, false);
      das::Serializer<infinit::DasUser> view(user);
      s.serialize_forward(view);
    }
    else
    {
      elle::serialization::json::SerializerOut s(*output, false);
      das::Serializer<infinit::DasPublicUser> view(user);
      s.serialize_forward(view);
    }
  }
  else if (mode.count("import"))
  {
    options_description import_options("Import options");
    import_options.add_options()
      ("input,i", value<std::string>(), "file to read user from "
                                        "(stdin by default)")
      ;
        auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --import [options]" << std::endl;
      output << std::endl;
      output << import_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto importation = parse_args(import_options, args);
    auto input = get_input(importation);
    {
      elle::serialization::json::SerializerIn s(*input, false);
      infinit::User user(s);
      ifnt.user_save(user);
      elle::printf("Imported user %s.\n", user.name);
    }
  }
  else if (mode.count("publish"))
  {
    options_description publish_options("Publish options");
    publish_options.add_options()
      ("name,n", value<std::string>(),
       "user to publish (defaults to system user)")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --publish --name USER [options]" << std::endl;
      output << std::endl;
      output << publish_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto publication = parse_args(publish_options, args);
    auto name = get_name(publication);
    auto user = ifnt.user_get(name);
    reactor::http::Request::Configuration c;
    c.header_add("Content-Type", "application/json");
    reactor::http::Request r(elle::sprintf("%s/users/%s", beyond, user.uid()),
                             reactor::http::Method::PUT,
                             std::move(c));

    {
      das::Serializer<infinit::DasPublicUser> view(user);
      elle::serialization::json::serialize(view, r, false);
    }
    r.finalize();
    reactor::wait(r);
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

int main(int argc, char** argv)
{
  program = argv[0];
  mode_options.add_options()
    ("export",   "export a user for someone else to import")
    ("generate", "create local user with a generated pair of keys")
    ("import",   "import a user")
    ("publish",  elle::sprintf("publish user to %s", beyond).c_str())
    ;
  options_description options("Infinit user utility");
  options.add(mode_options);
  return infinit::main(options, &network, argc, argv);
}
