#include <algorithm>
#include <iterator>

#include <elle/log.hh>

// Must be placed before main.hh.
ELLE_LOG_COMPONENT("infinit-drive");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

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
  infinit::Drive::Users users;
  infinit::Drive drive{name, owner, volume, network, desc ? *desc : "", users};
  ifnt.drive_save(drive);
  report_action("created", "drive", drive.name, std::string("locally"));

  if (aliased_flag(args, {"push-drive", "push"}))
  {
    auto url = elle::sprintf("drives/%s", drive.name);
    beyond_push(url, "drive", drive.name, drive, owner);
  }
}

static
std::vector<std::string>
_create_passports(
  std::unordered_map<std::string, infinit::Drive::User> const& invitees,
  std::string const& owner,
  infinit::Drive const& drive)
{
  using Passport = infinit::model::doughnut::Passport;

  std::vector<std::string> new_passports;
  auto self = ifnt.user_get(owner);
  for (auto const& invitee: invitees)
  {
    if (invitee.first == owner)
      continue;

    try
    {
      ifnt.passport_get(drive.network, invitee.first);
      /* If it exists, then do nothing. */
    }
    catch(MissingLocalResource const& e)
    {
      // Generate passport for this user.
      auto user = ifnt.user_get(invitee.first);
      auto pass = Passport(user.public_key,
                           drive.network,
                           self.private_key.get());
      ifnt.passport_save(pass);
      new_passports.push_back(user.name);
      report_created("passport",
                     elle::sprintf("%s: %s", drive.network, invitee.first));
    }
  }

  return new_passports;
}

static
void
_push_passports(infinit::Drive const& drive,
                std::vector<std::string> const& users,
                infinit::User const& owner)
{
  for (auto const& user: users)
  {
    if (user == owner.name)
      continue;

    auto passport = ifnt.passport_get(drive.network, user);
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", drive.network, user),
      "passport",
      elle::sprintf("%s: %s", drive.network, user),
      passport,
      owner);
  }
}

/*
 *  Compare the current drive json's invitee node with argument invitations.
 *  Add non-existing users.
 */
static
void
_update_local_json(infinit::Drive& drive,
  std::unordered_map<std::string, infinit::Drive::User> const& invitations)
{
  for (auto const& invitation: invitations)
  {
    if (drive.owner == invitation.first)
      continue;

    auto it = drive.users.find(invitation.first);
    if (it != drive.users.end())
      continue;

    drive.users[invitation.first] = invitation.second;
  }
  ifnt.drive_save(drive);
  report_action("created", "invitations for", drive.name, std::string("locally"));
}

COMMAND(invite)
{
  ELLE_TRACE_SCOPE("invite");
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto home = flag(args, "home");
  std::string permissions{"rw"};
  {
    auto o = optional(args, "permissions");
    if (o)
      permissions = *o;
  }

  auto users_count = args.count("user");
  auto users = [&] {
    if (users_count == 0)
      return std::vector<std::string>();
    return args["user"].as<std::vector<std::string>>();
  }();

  if (aliased_flag(args, { "fetch", "fetch-drive" }))
  {
    // First sync the drive from beyond.
    try
    {
      auto url = elle::sprintf("/drives/%s", name);
      auto drive = ifnt.drive_fetch(name);
      ifnt.drive_save(drive, true);
    }
    catch (MissingResource const& e)
    {
      if (e.what() != std::string("drive/not_found"))
        throw e;

      // The drive has not been pushed yet. No need to sync.
    }
  }

  auto drive = ifnt.drive_get(name);
  std::vector<std::string> new_passport_users;
  std::unordered_map<std::string, infinit::Drive::User> invitees;

  // If at least one --user is specified.
  if (args.count("user") != 0)
  {
    for (auto const& user: users)
      invitees[user] = {permissions, "pending", home};

    if (flag(args, "passports"))
      new_passport_users = _create_passports(invitees, owner.name, drive);

    _update_local_json(drive, std::move(invitees));
  }
  else if (flag(args, "passports"))
  {
    for (auto const& user: drive.users)
      if (user.second.status == "pending")
        invitees[user.first] = user.second;

    new_passport_users = _create_passports(invitees, owner.name, drive);
  }

  if (aliased_flag(args, { "push-drive", "push" }))
  {
    if (flag(args, "passports"))
      _push_passports(drive, new_passport_users, owner);

    // Only used if it invites only one user.
    std::string url = [&] {
      if (users.size() == 1)
      {
        ELLE_DEBUG("Invite one user");
        return elle::sprintf("drives/%s/invitations/%s", name, users.front());
      }
      else
      {
        ELLE_DEBUG("Invite many users");
        return elle::sprintf("drives/%s/invitations", name);
      }
    }();

    ELLE_DUMP("url: %s", url);

    try
    {
      if (users.size() == 1)
      {
        auto data = drive.users[users.front()];
        ELLE_DEBUG("data: %s", data);
        beyond_push(url, "invitation for", name, data, owner, true, true);
      }
      else
        beyond_push(url, "invitations for", name, drive.users, owner, true, true);
    }
    catch (MissingResource const& e)
    {
      if (e.what() == std::string("user/not_found"))
        not_found("NAME", "User");
      else if (e.what() == std::string("drive/not_found"))
        not_found(name, "Drive");
      else if (e.what() == std::string("passport/not_found"))
        not_found("NAME", "Passport");
      throw;
    }
    catch (BeyondError const& e)
    {
      if (e.error() == std::string("user/not_found"))
        not_found(e.name_opt(), "User");
      else if (e.error() == std::string("drive/not_found"))
        not_found(e.name_opt(), "Drive");
      else if (e.error() == std::string("passport/not_found"))
        not_found(e.name_opt(), "Passport");
      throw;
    }
  }
}

COMMAND(join)
{
  ELLE_TRACE_SCOPE("join");
  auto self = self_user(ifnt, args);
  auto drive = ifnt.drive_get(drive_name(args, self));

  if (self.name == boost::filesystem::path(drive.name).parent_path().string())
    throw elle::Error("The owner is automatically invited to its drives");
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
  auto self = self_user(ifnt, args);
  for (auto& drive: ifnt.drives_get())
  {
    std::cout << drive.name;
    if (drive.users.find(self.name) != drive.users.end())
      std::cout << ": " << drive.users[self.name].status;
    std::cout << std::endl;
  }
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
        "drives for user",
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
        { "push-drive", bool_switch(),
          elle::sprintf("push the created drive to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-drive" },
      },
    },
    {
      "invite",
      "Invite a user to join the drive",
      &invite,
      "--name DRIVE --user USER",
      {
        { "name,n", value<std::string>(), "drive to invite the user to" },
        { "user,u", value<std::vector<std::string>>()->multitoken(),
          "users to invite to the drive" },
        { "fetch-drive", bool_switch(), "update local drive descriptor" },
        { "fetch,f", bool_switch(), "alias for --fetch-drive" },
        { "push-drive", bool_switch(), "update remote drive descriptor" },
        { "push,p", bool_switch(), "alias for --push-drive" },
        { "passports", bool_switch(), "create passports for each invitee" },
      },
      {},
      // Hidden options.
      {
        { "permissions,p", value<std::string>(),
            "set default user permissions to XXX" },
        { "home,h", bool_switch(),
          "creates a home directory for the invited user" },
      },
    },
    {
      "join",
      "Join a drive you were invited to (Hub operation)",
      &join,
      "--name DRIVE",
      {
        { "name,n", value<std::string>(), "drive to invite the user on" },
      },
    },
    {
      "export",
      "Export a drive",
      &export_,
      "--name DRIVE",
      {
        { "name,n", value<std::string>(), "drive to export" },
      },
    },
    {
      "push",
      elle::sprintf("Push a drive to %s", beyond(true)).c_str(),
      &push,
      "--name NAME",
      {
        { "name,n", value<std::string>(),
          elle::sprintf("drive to push to %s", beyond(true)).c_str() },
      }
    },
    {
      "fetch",
      elle::sprintf("fetch drive from %s", beyond(true)).c_str(),
      &fetch,
      "",
      {
        { "name,n", value<std::string>(), "drive to fetch (optional)" },
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
        { "name,n", value<std::string>(), "drive to delete" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a drive from %s", beyond(true)).c_str(),
      &pull,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "drive to remove" },
      },
    },
  };
  return infinit::main("Infinit drive management utility", modes, argc, argv);
}
