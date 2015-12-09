#include <elle/log.hh>

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

void
upload_icon(infinit::User& self,
            infinit::Drive& drive,
            boost::filesystem::path const& icon_path);
void
fetch_icon(std::string const& name);
void
pull_icon(infinit::User& self,
          infinit::Drive& drive);
boost::optional<boost::filesystem::path>
icon_path(std::string const& name);

static
void
_push(variables_map const& args,
      infinit::User& user,
      infinit::Drive& drive)
{
  auto icon_path = optional(args, "icon");
  if (icon_path && icon_path.get().length() > 0)
  {
    if (!boost::filesystem::exists(icon_path.get()))
      throw CommandLineError(
        elle::sprintf("%s doesn't exist", icon_path.get()));
  }
  auto url = elle::sprintf("drives/%s", drive.name);
  beyond_push(url, "drive", drive.name, drive, user);
  if (icon_path)
  {
    if (icon_path.get().length() > 0)
      upload_icon(user, drive, icon_path.get());
    else
      pull_icon(user, drive);
  }
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
    _push(args, owner, drive);
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
  if (aliased_flag(args, {"fetch-drive", "fetch"}))
    fetch_(drive_name_);
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
  auto icon = icon_path(name);
  if (icon)
    drive.icon_path = icon.get().string();
  elle::serialization::json::serialize(drive, *output, false);
  report_exported(*output, "drive", drive.name);
}

COMMAND(push)
{
  ELLE_TRACE_SCOPE("push");
  auto owner = self_user(ifnt, args);
  auto name = drive_name(args, owner);
  auto drive = ifnt.drive_get(name);
  _push(args, owner, drive);
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
        "drives for user",
        self.name,
        self);
    for (auto const& drive: res["drives"])
    {
      ifnt.drive_save(drive);
    }
  }
}

template <typename Buffer>
void
_save_icon(std::string const& name,
           Buffer const& buffer)
{
  boost::filesystem::ofstream f;
  ifnt._open_write(f, ifnt._drive_icon_path(name),
                   name, "icon", true);
  f << buffer.string();
  report_action("fetched", "icon", name, std::string("locally"));
}

void
upload_icon(infinit::User& self,
            infinit::Drive& drive,
            boost::filesystem::path const& icon_path)
{
  boost::filesystem::ifstream icon;
  ifnt._open_read(icon, icon_path, self.name, "icon");
  std::string s(
    std::istreambuf_iterator<char>{icon},
    std::istreambuf_iterator<char>{});
  elle::ConstWeakBuffer data(s.data(), s.size());
  auto url = elle::sprintf("drives/%s/icon", drive.name);
  beyond_push_data(url, "icon", drive.name, data, "image/jpeg", self);
  _save_icon(drive.name, data);
}

void
fetch_icon(std::string const& name)
{
  auto url = elle::sprintf("drives/%s/icon", name);
  auto redirect = beyond_fetch<FakeRedirect>(url, "icon route", name);
  auto response = fetch_data(redirect.url, "icon", name)->response();
  // XXX: Deserialize XML.
  if (response.size() == 0 || response[0] == '<')
    throw MissingResource(
      elle::sprintf("icon for %s not found on %s", name, beyond(true)));
  _save_icon(name, response);
}

void
pull_icon(infinit::User& self,
          infinit::Drive& drive)
{
  auto url = elle::sprintf("drives/%s/icon", drive.name);
  beyond_delete(url, "icon", drive.name, self);
}

boost::optional<boost::filesystem::path>
icon_path(std::string const& name)
{
  auto path = ifnt._drive_icon_path(name);
  if (!boost::filesystem::exists(path))
    return boost::optional<boost::filesystem::path>{};
  return path;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  boost::program_options::option_description option_description =
    { "description,d", value<std::string>(), "created drive description" };
  boost::program_options::option_description option_icon =
    { "icon,i", value<std::string>(), "path to an image to use as icon"};
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
        option_description,
        option_icon,
        { "push-drive", bool_switch(),
          elle::sprintf("push the created drive to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-drive" },
      },
    },
    {
      "invite",
      "Invite a user to join the drive (Hub operation)",
      &invite,
      "--name DRIVE --user USER"
#ifndef INFINIT_PRODUCTION_BUILD
      " [--permissions]"
#endif
      ,
      {
        { "name,n", value<std::string>(), "drive to invite the user to" },
        { "user,u", value<std::string>(), "user to invite to the drive" },
        { "fetch-drive", bool_switch(), "update local drive descriptor" },
        { "fetch,f", bool_switch(), "alias for --fetch-drive" },
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
        option_icon,
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
