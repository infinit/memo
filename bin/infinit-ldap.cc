#include <elle/log.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/pem.hh>

#include <elle/ldap.hh>

#include <reactor/http/url.hh>

#include <boost/algorithm/string.hpp>

ELLE_LOG_COMPONENT("infinit-ldap");

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

COMMAND(populate_beyond)
{
  auto password = optional(args, "password");
  if (!password)
    password = read_passphrase("LDAP password");
  elle::ldap::LDAPClient ldap(mandatory(args, "server"),
                              elle::ldap::Attr(mandatory(args, "domain")),
                              mandatory(args, "user"),
                              *password);
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

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "populate-beyond",
      "Register LDAP users on beyond",
      &populate_beyond,
      {},
      {
        {"server", value<std::string>(), "URL to LDAP server"},
        {"domain,d", value<std::string>(), "LDAP domain"},
        {"user,u", value<std::string>(), "LDAP username"},
        {"password,p", value<std::string>(), "LDAP password"},
        {"searchbase,b", value<std::string>(), "search starting point (without domain)"},
        {"filter,f", value<std::string>(), "raw LDAP query to use"},
        {"object-class,o", value<std::string>(), "Filter results (default: person)"},
        {"username-pattern,U", value<std::string>(),
          "beyond unique username to set (default: $(cn)%)"},
        {"email-pattern,e", value<std::string>(),
          "email address pattern (default: $(mail)"},
        {"fullname-pattern,F", value<std::string>(),
          "fullname pattern (default: $(cn)"},
      },
    }
  };
  return infinit::main("Infinit ldap utility", modes, argc, argv, {});
}