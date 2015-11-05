#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-drive");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

#define COMMAND(name) static void name(variables_map const& args)

COMMAND(create)
{
  std::cout << "Not implemented yet." << std::endl;
}

COMMAND(invite)
{
  std::cout << "Not implemented yet." << std::endl;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  Modes modes {
    {
      "create",
      "Create a drive (a network and volume pair)",
      &create,
      "--name NAME --network NETWORK --volume VOLUME [--description DESCRIPTION]",
      {
        { "name,n", value<std::string>(), "created drive name" },
        { "network,N", value<std::string>(), "associated network name" },
        { "volume,v", value<std::string>(), "associated volume name" },
        { "description,d", value<std::string>(), "created drive description" },
        option_owner,
      },
    },
    {
      "invite",
      "Invite a user to join the drive",
      &invite,
      "--name DRIVE --user USER [--permissions]",
      {
        { "name,n", value<std::string>(), "drive to invite the user on" },
        { "user,u", value<std::string>(), "user to invite to the drive" },
        { "permissions,p", value<std::string>(), "set default user permissions to XXX" },
        option_owner,
      },
    },
  };
  return infinit::main("Infinit drive management utility", modes, argc, argv);
}
