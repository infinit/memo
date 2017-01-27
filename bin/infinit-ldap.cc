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
namespace bfs = boost::filesystem;

namespace
{
  using Strings = std::vector<std::string>;

  void
  extract_fields(std::string const& pattern, Strings& res)
  {
    size_t p = 0;
    while (true)
    {
      p = pattern.find_first_of('$', p);
      if (p == pattern.npos)
        return;
      auto pend = pattern.find_first_of(')', p);
      if (pend == pattern.npos)
        elle::err("malformed pattern: %s", pattern);
      res.emplace_back(pattern.substr(p+2, pend-p-2));
      p = pend+1;
    }
  }

  Strings
  extract_fields(std::string const& pattern)
  {
    auto res = Strings{};
    extract_fields(pattern, res);
    return res;
  }

  std::string
  make_field(std::string pattern,
             std::unordered_map<std::string, Strings> const& attrs,
             int i = 0)
  {
    using boost::algorithm::replace_all;
    for (auto& f: extract_fields(pattern))
      replace_all(pattern, "$(" + f + ")", attrs.at(f)[0]);
    replace_all(pattern, "%", i ? std::to_string(i) : "");
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
    return {server, domain, user, *password};
  }

  std::string
  make_filter(variables_map const& args,
              std::string const& default_object)
  {
    auto object_class = optional(args, "object-class");
    auto filter = optional(args, "filter");
    if (filter && object_class)
      elle::err("specify either --filter or --object-class");
    if (filter)
      return *filter;
    else
      return "objectClass=" + object_class.value_or(default_object);
  }

  std::unordered_map<std::string, infinit::User>
  _populate_network(boost::program_options::variables_map const& args,
                    std::string const& network_name)
  {
    auto owner = self_user(ifnt, args);
    auto network =
      infinit::NetworkDescriptor(ifnt.network_get(network_name, owner));
    auto mountpoint = mandatory(args, "mountpoint");
    enforce_in_mountpoint(mountpoint, false);
    auto ldap = make_ldap(args);
    auto searchbase = mandatory(args, "searchbase");
    auto results = [&]{
      auto filter = make_filter(args, "posixGroup");
      return ldap.search(searchbase, filter, {"cn", "memberUid"});
    }();
    // uids
    auto all_members = std::unordered_set<std::string>{};
    // uid -> dn
    auto dns = std::unordered_map<std::string, std::string>{};
    //gname -> uids
    auto groups = std::unordered_map<std::string, Strings>{};
    for (auto const& r: results)
    {
      if (r.find("memberUid") == r.end())
        // assume this is a user
        dns.insert(std::make_pair(r.at("dn")[0], r.at("dn")[0]));
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
      auto r = ldap.search(searchbase, "uid="+m);
      dns[m] = r.front().at("dn").front();
    }

    // uid -> user
    auto users = std::unordered_map<std::string, infinit::User>{};
    for (auto const& m: dns)
      try
      {
        auto u = infinit::beyond_fetch<infinit::User>(
          elle::sprintf("ldap_users/%s", reactor::http::url_encode(m.second)),
          "LDAP user",
          m.second);
        users.emplace(m.first, u);
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("Failed to fetch user %s from %s", m.second, infinit::beyond(true));
      }

    // Push all users
    for (auto const& u: users)
    {
      auto user_name = u.second.name;
      ELLE_TRACE("Pushing user %s (%s)", user_name, u.first);
      auto passport = infinit::model::doughnut::Passport(
        u.second.public_key,
        network.name,
        infinit::cryptography::rsa::KeyPair(owner.public_key,
                                            owner.private_key.get()),
        owner.public_key != network.owner,
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
          owner);
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
}

COMMAND(populate_network)
{
  auto network_name = mandatory(args, "network");
  _populate_network(args, network_name);
}

COMMAND(drive_invite)
{
  auto owner = self_user(ifnt, args);
  auto drive =
    ifnt.drive_get(ifnt.qualified_name(mandatory(args, "drive"), owner));
  auto network = ifnt.network_descriptor_get(drive.network, owner);
  bool create_home = flag(args, "create-home");
  using boost::algorithm::to_lower_copy;
  auto permissions = to_lower_copy(optional(args, "root-permissions").value_or("rw"));
  auto mode = [&]{
    auto allowed_modes = Strings{"r", "w", "rw", "none", ""};
    auto it = std::find(allowed_modes.begin(), allowed_modes.end(), permissions);
    if (it == allowed_modes.end())
      throw CommandLineError(
                             elle::sprintf("mode must be one of: %s", allowed_modes));
    auto modes_map = Strings{"setr", "setw", "setrw", "", ""};
    return modes_map[it - allowed_modes.begin()];
  }();
  auto mountpoint = mountpoint_root(mandatory(args, "mountpoint"), true);
  auto users = _populate_network(args, drive.network);
  for (auto const& u: users)
  {
    auto const& user_name = u.second.name;
    auto set_permissions = [&user_name] (bfs::path const& folder,
                                         std::string const& perms) {
      check(port_setxattr, folder.string(),
        elle::sprintf("user.infinit.auth.%s", perms), user_name, true);
    };
    if (!mode.empty()
        && u.second.public_key != network.owner)
      {
        try
        {
          set_permissions(mountpoint, mode);
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("Unable to set permissions on root directory (%s): %s",
                    mountpoint, e);
        }
      }
    if (create_home)
    {
      auto home_dir = mountpoint / "home";
      auto user_dir = home_dir / user_name;
      if (bfs::create_directories(user_dir))
      {
        if (u.second.public_key != network.owner)
        {
          try
          {
            if (mode.empty())
              set_permissions(mountpoint, "setr");
            set_permissions(home_dir, "setr");
            set_permissions(user_dir, "setrw");
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
                 user_name, user_dir);
      }
    }
    try
    {
      beyond_push(
        elle::sprintf("drives/%s/invitations/%s", drive.name, user_name),
        "invitation",
        elle::sprintf("%s: %s", drive.name, user_name),
        infinit::Drive::User(permissions, "ok", create_home),
        owner,
        true,
        true);
    }
    catch (elle::Error const& e)
    {
      ELLE_WARN("failed to push drive invite for %s: %s", user_name, e);
    }
  }
}

COMMAND(populate_hub)
{
  auto ldap = make_ldap(args);
  auto username_pattern = optional(args, "username-pattern").value_or("$(cn)%");
  auto email_pattern = optional(args, "email-pattern").value_or("$(mail)");
  auto fullname_pattern = optional(args, "fullname-pattern").value_or("$(cn)");
  auto res = [&] {
    auto searchbase = mandatory(args, "searchbase");
    auto filter = make_filter(args, "person");
    auto fields = [&] {
      auto res = Strings{};
      extract_fields(username_pattern, res);
      extract_fields(email_pattern, res);
      extract_fields(fullname_pattern, res);
      return res;
    }();
    ELLE_TRACE("will search %s and fetch fields %s", filter, fields);
    auto res = ldap.search(searchbase, filter, fields);
    ELLE_TRACE("LDAP returned %s", res);
    return res;
  }();

  // username -> fields
  auto missing = std::unordered_map<std::string, UserData>{};
  for (auto const& r: res)
  {
    auto dn = r.at("dn")[0];
    try
    {
      auto u = infinit::beyond_fetch<infinit::User>(
        elle::sprintf("ldap_users/%s", reactor::http::url_encode(dn)),
        "LDAP user",
        dn);
      ELLE_TRACE("got %s -> %s", dn, u.name);
    }
    catch (infinit::MissingResource const& e)
    {
      ELLE_TRACE("%s not on %s: %s", dn, infinit::beyond(true), e);
      for (int i=0; ; ++i)
      {
        auto username = make_field(username_pattern, r, i);
        try
        {
          auto u = infinit::beyond_fetch<infinit::User>(
            "user",
            reactor::http::url_encode(username));
          ELLE_TRACE("username %s taken", username);
          if (username_pattern.find('%') == std::string::npos)
          {
            ELLE_ERR("Username %s already taken, skipping %s", username, dn);
            break;
          }
        }
        catch (infinit::MissingResource const& e)
        {
          // all good
          missing.emplace(username,
                          UserData{dn,
                              make_field(fullname_pattern, r),
                              make_field(email_pattern, r)});
          break;
        }
      }
    }
  }

  if (missing.empty())
  {
    std::cout << std::endl << "No new users to register" << std::endl;
    return;
  }
  std::cout << std::endl << "Will register the following users:" << std::endl;
  for (auto& m: missing)
    elle::printf("%s: %s (%s) DN: %s\n",
                  m.first, m.second.fullname, m.second.email, m.second.dn);
  std::cout << std::endl;
  bool proceed = [&]
    {
      std::cout << "Proceed? [y/n] ";
      std::string line;
      std::getline(std::cin, line);
      boost::to_lower(line);
      return line == "y" || line == "yes";
    }();
  if (proceed)
    for (auto& m: missing)
    {
      auto u = infinit::User
        (m.first,
         infinit::cryptography::rsa::keypair::generate(2048),
         m.second.email, m.second.fullname, m.second.dn);
      ELLE_TRACE("pushing %s", u.name);
      infinit::beyond_push<das::Serializer<infinit::PrivateUserPublish>>
        ("user", u.name, u, u);
    }
  else
    std::cout << "Aborting..." << std::endl;
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
        { "network,N", value<std::string>(), "Network name" },
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
