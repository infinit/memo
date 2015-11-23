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
  ELLE_TRACE_SCOPE("create");
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto desc = optional(args, "description");

  auto network = ifnt.network_get(mandatory(args, "network"), owner);

  infinit::Volume volume;
  {
    auto name = ifnt.qualified_name(mandatory(args, "volume"), owner);
    volume = ifnt.volume_get(name);
  }

  infinit::Drive drive{name, owner, volume, network, desc ? *desc : "", {}};
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
  ELLE_TRACE_SCOPE("invite");
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

  infinit::Drive::User invitation{permissions, "pending", home};

  auto url = elle::sprintf("drives/%s/invitations/%s", drive_name_, user);

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
    else if (e.what() == std::string("passport/not_found"))
      not_found(user, "Passport");
    throw;
  }
  if (flag(args, "fetch"))
    fetch_(drive_name_);
}

COMMAND(join)
{
  ELLE_TRACE_SCOPE("join");
  auto self = self_user(ifnt, args);
  auto drive = ifnt.drive_get(drive_name(args, self));
  auto it = drive.users.find(self.name);
  if (it == drive.users.end())
    throw elle::Error(
      elle::sprintf("You haven't been invited to join %s", drive.name));
  auto invitation = it->second;
  invitation.status = "ok";
  auto url = elle::sprintf("drives/%s/invitations/%s", drive.name, self.name);
  try
  {
    beyond_push(url, "invitation", drive.name, invitation, self);
  }
  catch (MissingResource const& e)
  {
    if (e.what() == std::string("user/not_found"))
      not_found(self.name, "User"); // XXX: It might be the owner or you.
    else if (e.what() == std::string("drive/not_found"))
      not_found(drive.name, "Drive");
    throw;
  }
  drive.users[self.name] = invitation;
  ELLE_DEBUG("save drive %s", drive)
    ifnt.drive_save(drive);
}

COMMAND(export_)
{
  ELLE_TRACE_SCOPE("export");
  auto owner = self_user(ifnt, args);
  auto output = get_output(args);
  auto name = drive_name(args, owner);
  auto drive = ifnt.drive_get(name);
  elle::serialization::json::serialize(drive, *output, false);
  report_exported(*output, "drive", drive.name);
}


COMMAND(push)
{
  ELLE_TRACE_SCOPE("push");
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto drive = ifnt.drive_get(name);
  auto url = elle::sprintf("drives/%s", drive.name);
  beyond_push(url, "drive", drive.name, drive, owner);
}

COMMAND(delete_)
{
  ELLE_TRACE_SCOPE("delete");
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
  ELLE_TRACE_SCOPE("pull");
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  beyond_delete("drive", name, owner);
}

COMMAND(list)
{
  ELLE_TRACE_SCOPE("list");
  for (auto const& drive: ifnt.drives_get())
    std::cout << drive.name << std::endl;
}

static
void
fetch_(std::string const& drive_name)
{
  ELLE_TRACE_SCOPE("fetch %s", drive_name);
  auto remote_drive = ifnt.drive_fetch(drive_name);
  ELLE_DEBUG("save drive %s", remote_drive)
    ifnt.drive_save(remote_drive);
}

COMMAND(fetch)
{
  ELLE_TRACE_SCOPE("fetch");
  auto self = self_user(ifnt, args);
  if (optional(args, "name"))
  {
    ELLE_DEBUG("fetch specific drive");
    auto name = drive_name(args, self);
    fetch_(name);
  }
  else
  {
    ELLE_DEBUG("fetch all drives");
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Drive>>>(
        elle::sprintf("users/%s/drives", self.name),
        "drive for user",
        self.name,
        self);
    for (auto const& drive: res["drives"])
    {
      ifnt.drive_save(drive);
    }
  }
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
      "--name NAME --network NETWORK --volume VOLUME "
      "[--description DESCRIPTION]",
      {
        { "name,n", value<std::string>(), "created drive name" },
        { "network,N", value<std::string>(), "associated network name" },
        { "volume,v", value<std::string>(), "associated volume name" },
        { "description,d", value<std::string>(), "created drive description" },
        { "push,p", bool_switch(),
          elle::sprintf("push the created drive to %s", beyond(true)).c_str() },
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
      "join",
      "Join a drive you were invited to (Hub operation)",
      &join,
      "--name DRIVE",
      {
        { "name,n", value<std::string>(), "drive to invite the user on" },
        option_owner,
      },
    },
    {
      "export",
      "Export a drive",
      &export_,
      "--name DRIVE",
      {
        { "name,n", value<std::string>(), "drive to export" },
        option_owner,
      },
    },
    {
      "push",
      elle::sprintf("Push a drive to %s", beyond(true)).c_str(),
      &push,
      "--name NAME",
      {
        { "name,n", value<std::string>(),
          elle::sprintf("drive name to push to %s", beyond(true)).c_str() },
        option_owner,
      }
    },
    {
      "fetch",
      elle::sprintf("fetch drive from %s", beyond(true)).c_str(),
      &fetch,
      "",
      {
        { "name,n", value<std::string>(), "drive to fetch (optional)" },
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
      elle::sprintf("Delete a drive remotely from %s", beyond(true)).c_str(),
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
