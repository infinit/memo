#include <elle/log.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/pem.hh>

#include <elle/ldap.hh>

#include <reactor/http/url.hh>

#include <boost/algorithm/string.hpp>

ELLE_LOG_COMPONENT("infinit-ldap");

#include <xattrs.hh>
#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

using boost::program_options::variables_map;

static
void
extract_fields(std::string const& pattern, std::vector<std::string>& res)
{
  size_t p = 0;
  while (true)
  {
    p = pattern.find_first_of('$', p);
    if (p == pattern.npos)
      return;
    auto pend = pattern.find_first_of(')', p);
    if (pend == pattern.npos)
      throw elle::Error("malformed pattern: " + pattern);
    res.push_back(pattern.substr(p+2, pend-p-2));
    p = pend+1;
  }
}

static
std::string
make_field(
  std::string pattern,
  std::unordered_map<std::string, std::vector<std::string>> const& attrs,
  int i = 0)
{
  std::vector<std::string> fields;
  extract_fields(pattern, fields);
  for (auto& f: fields)
  {
    pattern = boost::algorithm::replace_all_copy(
      pattern, "$(" + f + ")", attrs.at(f)[0]);
  }
  if (i == 0)
    pattern = boost::algorithm::replace_all_copy(pattern, "%", std::string());
  else
    pattern = boost::algorithm::replace_all_copy(pattern, "%", std::to_string(i));
  return pattern;
}

struct UserData
{
  std::string dn;
  std::string fullname;
  std::string email;
};

elle::ldap::LDAPClient
make_ldap(variables_map const& args)
{
  auto server = mandatory(args, "server");
  auto domain = mandatory(args, "domain");
  auto user = mandatory(args, "user");
  auto password = optional(args, "password");
  if (!password)
    password = read_passphrase("LDAP password");
  elle::ldap::LDAPClient ldap(server, domain, user, *password);
  return ldap;
}

static
std::unordered_map<std::string, infinit::User>
_populate_network(boost::program_options::variables_map const& args,
                  std::string const& network_name)
{
  auto self = self_user(ifnt, args);
  auto network =
    infinit::NetworkDescriptor(ifnt.network_get(network_name, self));
  auto mountpoint = mandatory(args, "mountpoint");
  enforce_in_mountpoint(mountpoint, true, true);
  auto ldap = make_ldap(args);
  auto searchbase = mandatory(args, "searchbase");
  auto objectclass = optional(args, "object-class");
  auto filter = optional(args, "filter");
  if (filter && objectclass)
    throw elle::Error("specify either --filter or --object-class");
  if (objectclass)
    filter = "objectClass=" + *objectclass;
  else if (!filter)
    filter = "objectClass=posixGroup";
  auto res = ldap.search(searchbase, *filter, {"cn", "memberUid"});

  std::unordered_set<std::string> all_members; // uids
  std::unordered_map<std::string, std::string> dns; // uid -> dn

  std::unordered_map<std::string, std::vector<std::string>> groups; //gname -> uids
  for (auto const& r: res)
  {
    if (r.find("memberUid") == r.end())
    { // assume this is an user
      dns.insert(std::make_pair(r.at("dn")[0], r.at("dn")[0]));
    }
    else
    {
      auto name = r.at("cn")[0];
      auto members = r.at("memberUid");
      all_members.insert(members.begin(), members.end());
      groups[name] = members;
    }
  }

  for (auto const& m: all_members)
  {
    auto r = ldap.search(searchbase, "uid="+m, {});
    dns[m] = r.front().at("dn").front();
  }

  std::unordered_map<std::string, infinit::User> users; // uid -> user
  for (auto const& m: dns)
  {
    try
    {
      auto u = beyond_fetch<infinit::User>(
        elle::sprintf("ldap_users/%s", reactor::http::url_encode(m.second)),
        "LDAP user",
        m.second);
      users.insert(std::make_pair(m.first, u));
    }
    catch (elle::Error const& e)
    {
      ELLE_WARN("Failed to fetch user %s from %s", m.second, beyond(true));
    }
  }

  // Push all users
  for (auto const& u: users)
  {
    auto user_name = u.second.name;
    ELLE_TRACE("Pushing user %s (%s)", user_name, u.first);
    infinit::model::doughnut::Passport passport(
      u.second.public_key,
      network.name,
      infinit::cryptography::rsa::KeyPair(self.public_key,
                                          self.private_key.get()),
      self.public_key != network.owner,
      !flag(args, "deny-write"),
      !flag(args, "deny-storage"),
      false);
    try
    {
      beyond_push(
        elle::sprintf("networks/%s/passports/%s", network.name, user_name),
        "passport",
        elle::sprintf("%s: %s", network.name, user_name),
        passport,
        self);
    }
    catch (elle::Error const& e)
    {
      ELLE_WARN("Failed to push passport for %s: %s", user_name, e);
    }
    auto passport_ser = elle::serialization::json::serialize(passport, false);
    char buf[4096];
    int res = port_getxattr(mountpoint,
                            elle::sprintf("infinit.resolve.%s", user_name),
                            buf, 4095, true);
    if (res > 0)
    {
      std::cout
         << elle::sprintf("User \"%s\" already registerd to network.", user_name)
         << std::endl;
      continue;
    }
    res = port_setxattr(mountpoint, "infinit.register." + user_name,
                        passport_ser.string(), true);
    if (res)
      ELLE_WARN("Failed to set user %s: %s", user_name, res);
    else
    {
      std::cout << elle::sprintf("Registered user \"%s\" to network.",
                                 user_name)
                << std::endl;
    }
  }
  // Push groups
  for (auto const& g: groups)
  {
    ELLE_TRACE_SCOPE("Creating group %s", g.first);
    char buf[4096];
    int res = port_getxattr(mountpoint,
                            elle::sprintf("infinit.resolve.%s", g.first),
                            buf, 4095, true);
    if (res > 0)
    {
      std::cout
        << elle::sprintf("Group \"%s\" already exists on network.", g.first)
        << std::endl;
    }
    else
    {
      port_setxattr(mountpoint, "infinit.group.create", g.first, true);
      std::cout << elle::sprintf("Added group \"%s\" to network.", g.first)
                << std::endl;
    }
    for (auto const& m: g.second)
    {
      if (users.find(m) == users.end())
      {
        ELLE_TRACE("skipping user %s", m);
        continue;
      }
      ELLE_TRACE("adding user %s", m);
      int res = port_setxattr(mountpoint, "infinit.group.add",
        g.first + ':' + users.at(m).name, true);
      if (res)
        ELLE_LOG("Failed to add %s to group %s: %s", m, g.first, res);
      else
      {
        std::cout << elle::sprintf(
          "Added user \"%s\" to group \"%s\" on network.", m, g.first);
        std::cout << std::endl;
      }
    }
  }
  return users;
}

COMMAND(populate_network)
{
  auto network_name = mandatory(args, "network");
  _populate_network(args, network_name);
}

COMMAND(drive_invite)
{
  auto self = self_user(ifnt, args);
  auto drive =
    ifnt.drive_get(ifnt.qualified_name(mandatory(args, "drive"), self));
  auto network = ifnt.network_descriptor_get(drive.network, self);
  bool create_home = flag(args, "create-home");
  std::string permissions = "rw";
  auto perm_arg = optional(args, "root-permissions");
  if (perm_arg)
  {
    permissions = *perm_arg;
    std::transform(permissions.begin(), permissions.end(), permissions.begin(),
                   ::tolower);
  }
  std::vector<std::string> allowed_modes = {"r", "w", "rw", "none", ""};
  auto it = std::find(allowed_modes.begin(), allowed_modes.end(), permissions);
  if (it == allowed_modes.end())
  {
    throw CommandLineError(
      elle::sprintf("mode must be one of: %s", allowed_modes));
  }
  std::vector<std::string> modes_map = {"setr", "setw", "setrw", "", ""};
  auto mode = modes_map[it - allowed_modes.begin()];
  auto mountpoint = mountpoint_root(mandatory(args, "mountpoint"), true);
  auto users = _populate_network(args, drive.network);
  for (auto const& u: users)
  {
    auto set_permissions = [] (boost::filesystem::path const& folder,
                               std::string const& perms,
                               std::string const& user_name) {
      check(port_setxattr, folder.string(),
        elle::sprintf("user.infinit.auth.%s", perms), user_name, true);
    };
    if (!mode.empty())
    {
      if (u.second.public_key != network.owner)
      {
        try
        {
          set_permissions(mountpoint, mode, u.second.name);
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("Unable to set permissions on root directory (%s): %s",
                    mountpoint, e);
        }
      }
    }
    if (create_home)
    {
      auto home_dir = mountpoint / "home";
      auto user_dir = home_dir / u.second.name;
      if (boost::filesystem::create_directories(user_dir))
      {
        if (u.second.public_key != network.owner)
        {
          try
          {
            if (mode.empty())
              set_permissions(mountpoint, "setr", u.second.name);
            set_permissions(home_dir, "setr", u.second.name);
            set_permissions(user_dir, "setrw", u.second.name);
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN(
              "Unable to set permissions on user home directory (%s): %s",
               user_dir, e);
          }
        }
        std::cout << elle::sprintf("Created home directory: %s.", user_dir)
                  << std::endl;
      }
      else
      {
        ELLE_WARN("Unable to create home directory for %s: %s.",
                 u.second.name, user_dir);
      }
    }
    try
    {
      beyond_push(
        elle::sprintf("drives/%s/invitations/%s", drive.name, u.second.name),
        "invitation",
        elle::sprintf("%s: %s", drive.name, u.second.name),
        infinit::Drive::User(permissions, "ok", create_home),
        self,
        true,
        true);
    }
    catch (elle::Error const& e)
    {
      ELLE_WARN("failed to push drive invite for %s: %s", u.second.name, e);
    }
  }
}

COMMAND(populate_hub)
{
  auto ldap = make_ldap(args);
  auto searchbase = mandatory(args, "searchbase");
  auto objectclass = optional(args, "object-class");
  auto filter = optional(args, "filter");
  if (filter && objectclass)
    throw elle::Error("specify either --filter or --object-class");
  if (objectclass)
    filter = "objectClass=" + *objectclass;
  else if (!filter)
    filter = "objectClass=person";
  auto pattern = optional(args, "username-pattern");
  if (!pattern)
    pattern = "$(cn)%";
  auto email_pattern = optional(args, "email-pattern");
  if (!email_pattern)
    email_pattern = "$(mail)";
  auto fullname_pattern = optional(args, "fullname-pattern");
  if (!fullname_pattern)
    fullname_pattern = "$(cn)";
  std::vector<std::string> fields;
  extract_fields(*pattern, fields);
  extract_fields(*email_pattern, fields);
  extract_fields(*fullname_pattern, fields);
  ELLE_TRACE("will search %s and fetch fields %s", *filter, fields);
  auto res = ldap.search(searchbase, *filter, fields);
  ELLE_TRACE("LDAP returned %s", res);

  // username -> fields
  std::unordered_map<std::string, UserData> missing;
  for (auto const& r: res)
  {
    auto dn = r.at("dn")[0];
    try
    {
      auto u = beyond_fetch<infinit::User>(
        elle::sprintf("ldap_users/%s", reactor::http::url_encode(dn)),
        "LDAP user",
        dn);
      ELLE_TRACE("got %s -> %s", dn, u.name);
    }
    catch (MissingResource const& e)
    {
      ELLE_TRACE("%s not on %s: %s", dn, beyond(true), e);
      for (int i=0; ; ++i)
      {
        auto username = make_field(*pattern, r, i);
        try
        {
          auto u = beyond_fetch<infinit::User>(
            "user",
            reactor::http::url_encode(username));
          ELLE_TRACE("username %s taken", username);
          if ((*pattern).find('%') == std::string::npos)
          {
            ELLE_ERR("Username %s already taken, skipping %s", username, dn);
            break;
          }
          continue;
        }
        catch (MissingResource const& e)
        {
          // all good
          missing[username] = UserData{dn,
            make_field(*fullname_pattern, r),
          make_field(*email_pattern, r)};
          break;
        }
      }
    }
  }

  if (!missing.size())
  {
    std::cout << std::endl << "No new users to register" << std::endl;
    return;
  }
  std::cout << std::endl << "Will register the following users:" << std::endl;
  for (auto& m: missing)
  {
    std::cout << elle::sprintf(
      "%s: %s (%s) DN: %s",
      m.first, m.second.fullname, m.second.email, m.second.dn) << std::endl;
  }
  std::cout << std::endl;
  std::cout << "Proceed? [y/n] ";
  std::string line;
  std::getline(std::cin, line);
  std::transform(line.begin(), line.end(), line.begin(), ::tolower);
  if (line != "y")
  {
    std::cout << "Aborting..." << std::endl;
    return;
  }
  for (auto& m: missing)
  {
    infinit::User u(m.first,
                    infinit::cryptography::rsa::keypair::generate(2048),
                    m.second.email, m.second.fullname, m.second.dn);
    das::Serializer<infinit::DasPrivateUserPublish> view{u};
    ELLE_TRACE("pushing %s", u.name);
    beyond_push("user", u.name, view, u);
  }
}

#define LDAP_CORE_OPTIONS                                     \
  { "server", value<std::string>(), "URL of LDAP server" },   \
  { "domain,d", value<std::string>(), "LDAP domain" },        \
  { "user,u", value<std::string>(), "LDAP username" },        \
  { "password,p", value<std::string>(), "LDAP password" }

#define LDAP_NETWORK_OPTIONS                                                  \
  { "searchbase,b", value<std::string>(),                                     \
    "search starting point (without domain)" },                               \
  { "filter,f", value<std::string>(),                                         \
    "raw LDAP query to use\n(default: objectClass=posixGroup)" },             \
  { "object-class,o", value<std::string>(),                                   \
    "Filter results (default: posixGroup)" },                                 \
  { "mountpoint,m", value<std::string>(),                                     \
    "Path to a mounted volume of the network" },                              \
  { "as", value<std::string>(), "Infinit user to use" },                      \
  { "deny-write", bool_switch(), "Create a passport for read-only access" },  \
  { "deny-storage", bool_switch(),                                            \
    "Create a passport that cannot contribute storage" }

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "populate-hub",
      "Register LDAP users on the Hub",
      &populate_hub,
      "--server SERVER --domain DOMAIN --user USER --searchbase SEARCHBASE",
      {
        LDAP_CORE_OPTIONS,
        { "searchbase,b", value<std::string>(),
          "search starting point (without domain)" },
        { "filter,f", value<std::string>(),
          "raw LDAP query to use\n(default: objectClass=person)" },
        { "object-class,o", value<std::string>(),
          "Filter results (default: person)" },
        { "username-pattern,U", value<std::string>(),
          "Hub unique username to set\n(default: $(cn)%). Remove the '%' "
          "to disable unique username generator"},
        { "email-pattern,e", value<std::string>(),
          "email address pattern (default: $(mail))" },
        { "fullname-pattern,F", value<std::string>(),
          "fullname pattern (default: $(cn))" },
      },
    },
    {
      "populate-network",
      "Register LDAP users and groups to a network",
      &populate_network,
      "--server SERVER --domain DOMAIN --user USER --searchbase SEARCHBASE "
      "--network NETWORK --mountpoint MOUNTPOINT",
      {
        LDAP_CORE_OPTIONS,
        { "network,n", value<std::string>(), "Network name" },
        LDAP_NETWORK_OPTIONS,
      },
    },
    {
      "drive-invite",
      "Invite LDAP users to a drive",
      &drive_invite,
      "--server SERVER --domain DOMAIN --user USER --searchbase SEARCHBASE "
      "--drive DRIVE --mountpoint MOUNTPOINT",
      {
        LDAP_CORE_OPTIONS,
        { "drive", value<std::string>(), "drive to invite users to" },
        { "root-permissions", value<std::string>(),
          "Volume root permissions to give\n(r,rw,none – default: rw)" },
        { "create-home", bool_switch(),
          "Create user home directory of the form home/<user>" },
        LDAP_NETWORK_OPTIONS,
      },
    },
  };
  return infinit::main("Infinit LDAP utility", modes, argc, argv, {});
}
