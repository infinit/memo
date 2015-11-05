#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-drive");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

#define COMMAND(name) static void name(variables_map const& args)

static
std::string
drive_name(variables_map const& args, infinit::User const& owner)
{
  return ifnt.qualified_name(mandatory(args, "name"), owner);
}

COMMAND(create)
{
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto desc = optional(args, "description");

  auto network = ifnt.network_get(mandatory(args, "network"), owner);

  infinit::Volume volume;
  {
    auto name = ifnt.qualified_name(mandatory(args, "volume"), owner);
    volume = ifnt.volume_get(name);
  }

  infinit::Drive drive{name, volume.name, network.name, desc ? *desc : ""};
  ifnt.drive_save(drive);

  auto push = flag(args, "push");
  if (push)
  {
    auto url = elle::sprintf("drives/%s", drive.name);
    beyond_push(url, "drive", drive.name, drive, owner);
  }
}

COMMAND(invite)
{
  std::cout << "Not implemented yet." << std::endl;
}

COMMAND(push)
{
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto drive = ifnt.drive_get(name);
  auto url = elle::sprintf("drives/%s", drive.name);
  beyond_push(url, "drive", drive.name, drive, owner);
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
        { "push,p", bool_switch(), "push the created drive on the hub" },
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
    {
      "push",
      "Push a drive on the hub",
      &push,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "drive name to push on the hub" },
        option_owner,
      }
    }
  };
  return infinit::main("Infinit drive management utility", modes, argc, argv);
}
