#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-drive");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

#define COMMAND(name) static void name(variables_map const& args)

static
void
fetch_(std::string const& drive_name);

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

  infinit::Drive drive{name, volume.name, network.name, desc ? *desc : "", {}};
  ifnt.drive_save(drive);
  report_action("created", "drive", drive.name, std::string("locally"));

  auto push = flag(args, "push");
  if (push)
  {
    auto url = elle::sprintf("drives/%s", drive.name);
    beyond_push(url, "drive", drive.name, drive, owner);
  }
}

COMMAND(invite)
{
  auto owner = self_user(ifnt, args);
  auto drive_name_ = drive_name(args, owner);
  auto user = mandatory(args, "user");
  auto home = flag(args, "home");
  // FIXME: for now the permissions option is a flag yet should be
  // DEFAULT,R,W,X,RW,RX,WX,RWX (and/or octal notation ?)
  std::string permissions{"rw"};
  {
    auto o = optional(args, "permissions");
    if (o)
      permissions = *o;
  }

  infinit::DriveUsers invitation{permissions,
                                 "pending",
                                 home};

  auto url = elle::sprintf("drives/%s/invite/%s", drive_name_, user);

  try
  {
    beyond_push(url, "invitation", drive_name_, invitation, owner);
  }
  catch (MissingResource const& e)
  {
    if (e.what() == std::string("user/not_found"))
      not_found(user, "User");
    else if (e.what() == std::string("drive/not_found"))
      not_found(drive_name_, "Drive");
    return;
  }
  if (flag(args, "fetch"))
    fetch_(drive_name_);
}

COMMAND(push)
{
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto drive = ifnt.drive_get(name);
  auto url = elle::sprintf("drives/%s", drive.name);
  beyond_push(url, "drive", drive.name, drive, owner);
}

COMMAND(delete_)
{
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto path = ifnt._drive_path(name);
  if (boost::filesystem::remove(path))
    report_action("deleted", "drive", name, std::string("locally"));
  else
    throw MissingLocalResource(
      elle::sprintf("File for drive could not be deleted: %s", path));
}

COMMAND(pull)
{
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  beyond_delete("drive", name, owner);
}

COMMAND(list)
{
  for (auto const& drive: ifnt.drives_get())
    std::cout << drive.name << std::endl;
}

static
void
fetch_(std::string const& drive_name)
{
  auto remote_drive = ifnt.drive_fetch(drive_name);
  ifnt.drive_delete(drive_name);
  ifnt.drive_save(remote_drive);
}

COMMAND(fetch)
{
  auto self = self_user(ifnt, args);
  auto drive_name = ifnt.qualified_name(mandatory(args, "name"), self);
  fetch_(drive_name);
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
      "Invite a user to join the drive (Hub operation)",
      &invite,
      "--name DRIVE --user USER [--permissions]",
      {
        { "name,n", value<std::string>(), "drive to invite the user on" },
        { "user,u", value<std::string>(), "user to invite to the drive" },
        { "permissions,p", value<std::string>(), "set default user permissions to XXX" },
        { "home,h", bool_switch(), "creates a home directory for the invited user" },
        { "fetch,f", bool_switch(), "update local drive descriptor" },
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
    },
    {
      "fetch",
      "Fetch a drive",
      &fetch,
      "",
      {
        { "name,n", value<std::string>(), "drive to fetch" },
        option_owner,
      },
    },
    {
      "list",
      "List drives",
      &list,
      {},
    },
    {
      "delete",
      "Delete a drive locally",
      &delete_,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "drive name to delete locally" },
        option_owner,
      }
    },
    {
      "pull",
      "Delete a drive remotly on the hub",
      &pull,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "drive name to delete remotely" },
        option_owner,
      }
    }
  };
  return infinit::main("Infinit drive management utility", modes, argc, argv);
}
