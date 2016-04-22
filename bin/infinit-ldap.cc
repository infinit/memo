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
make_field(std::string pattern,
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

elle::ldap::LDAPClient make_ldap(variables_map const& args)
{
  auto password = optional(args, "password");
  if (!password)
    password = read_passphrase("LDAP password");
  elle::ldap::LDAPClient ldap(mandatory(args, "server"),
                              elle::ldap::Attr(mandatory(args, "domain")),
                              mandatory(args, "user"),
                              *password);
  return ldap;
}

COMMAND(populate_network)
{
  auto self = self_user(ifnt, args);
  auto network = infinit::NetworkDescriptor(ifnt.network_get(mandatory(args, "network"), self));
  auto mountpoint = mandatory(args, "mountpoint");
  auto ldap = make_ldap(args);
  auto searchbase = mandatory(args, "searchbase");
  auto objectclass = optional(args, "object-class");
  auto filter = optional(args, "filter");
  if (filter && objectclass)
    throw elle::Error("filter and object-class can't both be specified");
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
  { // FIXME: batch
    auto r = ldap.search(elle::ldap::Attr(), "uid="+m, {});
    dns[m] = r.front().at("dn").front();
  }

  std::unordered_map<std::string, infinit::User> users; // uid -> user
  for (auto const& m: dns)
  {
    try
    {
      auto u = beyond_fetch<infinit::User>("ldap_user",
                                           reactor::http::url_encode(m.second));
      users.insert(std::make_pair(m.first, u));
    }
    catch (elle::Error const& e)
    {
      ELLE_LOG("Failed to fetch user %s from %s", m.second, beyond(true));
    }
  }

  // Push all users
  for (auto const& u: users)
  {
    ELLE_TRACE("Pusing user %s (%s)", u.second.name, u.first);
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
        elle::sprintf("networks/%s/passports/%s", network.name, u.second.name),
        "passport",
        elle::sprintf("%s: %s", network.name, u.second.name),
        passport,
        self);
    }
    catch (elle::Error const& e)
    {
      ELLE_LOG("failed to push passport: %s", e);
    }
    auto passport_ser = elle::serialization::json::serialize(passport, false);
    int res = port_setxattr(mountpoint, "infinit.register." + u.second.name,
                            passport_ser.string(), true);
    if (res)
      ELLE_LOG("Failed to set user %s: %s", u.second.name, res);
  }
  // Push groups
  for (auto const& g: groups)
  {
    ELLE_TRACE_SCOPE("Creating group %s", g.first);
    port_setxattr(mountpoint, "infinit.group.create", g.first, true);
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
    }
  }
}

COMMAND(populate_beyond)
{
  auto ldap = make_ldap(args);
  auto searchbase = mandatory(args, "searchbase");
  auto objectclass = optional(args, "object-class");
  auto filter = optional(args, "filter");
  if (filter && objectclass)
    throw elle::Error("filter and object-class can't both be specified");
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
  ELLE_TRACE("Ldap returned %s", res);

  // username -> fields
  std::unordered_map<std::string, UserData> missing;
  for (auto const& r: res)
  {
    auto dn = r.at("dn")[0];
    try
    {
      auto u = beyond_fetch<infinit::User>("ldap_user",
                                           reactor::http::url_encode(dn));
      ELLE_TRACE("got %s -> %s", dn, u.name);
    }
    catch (MissingResource const& e)
    {
      ELLE_TRACE("%s not in beyond: %s", dn, e);
      for (int i=0; ; ++i)
      {
        auto username = make_field(*pattern, r, i);
        try
        {
          auto u = beyond_fetch<infinit::User>("user",
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

  std::cout << "Will register the following users:" << std::endl;
  for (auto& m: missing)
    std::cout << elle::sprintf("%s: email %s  fullname %s  dn %s",
                               m.first, m.second.email, m.second.fullname,
                               m.second.dn) << std::endl;
  std::cout << std::endl;
  std::cout << "Proceeed ? (y/n)" << std::endl;
  std::string line;
  std::getline(std::cin, line);
  if (line != "y")
  {
    std::cout << "aborting" << std::endl;
    return;
  }
  for (auto& m: missing)
  {
    infinit::User u(m.first, infinit::cryptography::rsa::keypair::generate(2048),
                    m.second.email, m.second.fullname, m.second.dn);
    das::Serializer<infinit::DasPrivateUserPublish> view{u};
    ELLE_TRACE("pushing %s", u.name);
    beyond_push("user", u.name, view, u);
  }
}

#define LDAP_CORE_OPTIONS \
        {"server", value<std::string>(), "URL to LDAP server"},     \
        {"domain,d", value<std::string>(), "LDAP domain"},          \
        {"user,u", value<std::string>(), "LDAP username"},          \
        {"password,p", value<std::string>(), "LDAP password"}
int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "populate-network",
      "Register LDAP users and groups to a network",
      &populate_network,
      {},
      {
        LDAP_CORE_OPTIONS,
        {"searchbase,b", value<std::string>(), "search starting point (without domain)"},
        {"filter,f", value<std::string>(), "raw LDAP query to use (default: objectClass=posixGroup)"},
        {"object-class,o", value<std::string>(), "Filter results (default: posixGroup)"},
        {"mountpoint,m", value<std::string>(), "Path to a mounted volume of the network"},
        {"network,n", value<std::string>(), "Network name"},
        {"as", value<std::string>(), "user"},
      }
    },
    {
      "populate-beyond",
      "Register LDAP users on beyond",
      &populate_beyond,
      {},
      {
        LDAP_CORE_OPTIONS,
        {"searchbase,b", value<std::string>(), "search starting point (without domain)"},
        {"filter,f", value<std::string>(), "raw LDAP query to use (default: objectClass=person)"},
        {"object-class,o", value<std::string>(), "Filter results (default: person)"},
        {"username-pattern,U", value<std::string>(),
          "beyond unique username to set (default: $(cn)%). Remove the '%'"
          "to disable unique username generator"},
        {"email-pattern,e", value<std::string>(),
          "email address pattern (default: $(mail)"},
        {"fullname-pattern,F", value<std::string>(),
          "fullname pattern (default: $(cn)"},
      },
    }
  };
  return infinit::main("Infinit ldap utility", modes, argc, argv, {});
}