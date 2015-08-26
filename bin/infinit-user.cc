#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>

ELLE_LOG_COMPONENT("infinit-user");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

static
void
export_(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  auto output = get_output(args);
  if (args.count("full") && args["full"].as<bool>())
  {
    elle::fprintf(std::cerr, "WARNING: you are exporting the user \"%s\" "
                  "including the private key\n", name);
    elle::fprintf(std::cerr, "WARNING: anyone in possession of this "
                  "information can impersonate that user\n");
    elle::fprintf(std::cerr, "WARNING: if you mean to export your user for "
                  "someone else, remove the --full flag\n");
    das::Serializer<infinit::DasUser> view(user);
    elle::serialization::json::serialize(view, *output, false);
  }
  else
  {
    das::Serializer<infinit::DasPublicUser> view(user);
    elle::serialization::json::serialize(view, *output, false);
  }
}

static
void
generate(variables_map const& args)
{
  auto name = get_name(args);
  infinit::User user(name,
                     infinit::cryptography::rsa::keypair::generate(2048));
  ifnt.user_save(user);
  elle::printf("Generated user %s.\n", name);
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
    elle::printf("Imported user %s.\n", user.name);
  }
}

static
void
publish(variables_map const& args)
{
  auto name = get_name(args);
  auto user = ifnt.user_get(name);
  das::Serializer<infinit::DasPublicUser> view(user);
  beyond_publish("user", user.uid(), view);
}

int
main(int argc, char** argv)
{
  program = argv[0];
  Modes modes {
    {
      "export",
      "Export a user for someone else to import",
      &export_,
      {},
      {
        { "name,n", value<std::string>(),
          "user to export (defaults to system user)" },
        { "full,f", bool_switch(),
          "also export private information "
          "(do not use this unless you understand the implications)" },
        { "output,o", value<std::string>(),
          "file to write user to (defaults to stdout)" },
      },
    },
    {
      "generate",
      "Create user with a generated pair of keys",
      &generate,
      {},
      {
        { "name,n", value<std::string>(),
          "user name (defaults to system username)" },
      },
    },
    {
      "import",
      "Import a user",
      &import,
      {},
      {
        { "input,i", value<std::string>(),
          "file to read user from (defaults to stdin)" },
      },
    },
    {
      "publish",
      elle::sprintf("Publish user to %s", beyond()).c_str(),
      &publish,
      {},
      {
        { "name,n", value<std::string>(),
          "user to publish (defaults to system user)" },
      },
    },
  };
  return infinit::main("Infinit user utility", modes, argc, argv);
}
