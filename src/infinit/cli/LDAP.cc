#include <infinit/cli/LDAP.hh>

#include <elle/ldap.hh>

#include <reactor/http/url.hh>

#include <infinit/Drive.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/cli/User.hh>
#include <infinit/cli/utility.hh>
#include <infinit/cli/xattrs.hh>

ELLE_LOG_COMPONENT("cli.ldap");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    LDAP::LDAP(Infinit& infinit)
      : Object(infinit)
      , drive_invite(
        "Invite LDAP users to a drive",
        das::cli::Options(),
        this->bind(modes::mode_drive_invite,
                   cli::server,
                   cli::domain,
                   cli::user,
                   cli::password = boost::none,
                   cli::drive,
                   cli::root_permissions = "rw",
                   cli::create_home = false,
                   cli::searchbase,
                   cli::filter = boost::none,
                   cli::object_class = boost::none,
                   cli::mountpoint,
                   cli::deny_write = false,
                   cli::deny_storage = false))
      , populate_network(
        "Register LDAP users and groups to a network",
        das::cli::Options(),
        this->bind(modes::mode_populate_network,
                   cli::server,
                   cli::domain,
                   cli::user,
                   cli::password = boost::none,
                   cli::network,
                   cli::searchbase,
                   cli::filter = boost::none,
                   cli::object_class = boost::none,
                   cli::mountpoint,
                   cli::deny_write = false,
                   cli::deny_storage = false))
    {}

    /*----------.
    | Helpers.  |
    `----------*/

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

      std::string
      make_filter(boost::optional<std::string> const& filter,
                  boost::optional<std::string> const& object_class,
                  std::string const& default_object)
      {
        if (filter && object_class)
          elle::err("specify either --filter or --object-class");
        if (filter)
          return *filter;
        else
          return "object_class=" + object_class.value_or(default_object);
      }

      std::unordered_map<std::string, infinit::User>
      _populate_network(Infinit& cli,
                        std::string const& server,
                        std::string const& domain,
                        std::string const& user,
                        boost::optional<std::string> const& password,

                        std::string const& network_name,

                        std::string const& searchbase,
                        boost::optional<std::string> const& filter,
                        boost::optional<std::string> const& object_class,
                        std::string const& mountpoint,
                        bool deny_write,
                        bool deny_storage)
      {
        auto& ifnt = cli.infinit();
        auto owner = cli.as_user();
        auto network =
          infinit::NetworkDescriptor(ifnt.network_get(network_name, owner));
        enforce_in_mountpoint(mountpoint, false);
        auto ldap = elle::ldap::LDAPClient{
          server, domain, user,
          password.value_or_eval([&] { return cli.read_secret("LDAP password"); })
        };
        auto results = [&]{
          auto filt = make_filter(filter, object_class, "posixGroup");
          return ldap.search(searchbase, filt, {"cn", "memberUid"});
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
          auto r = ldap.search(searchbase, "uid="+m, {});
          dns[m] = r.front().at("dn").front();
        }

        // uid -> user
        auto res = std::unordered_map<std::string, infinit::User>{};
        for (auto const& m: dns)
          try
          {
            auto u = ifnt.beyond_fetch<infinit::User>(
              elle::sprintf("ldap_users/%s", reactor::http::url_encode(m.second)),
              "LDAP user",
              m.second);
            res.emplace(m.first, u);
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("Failed to fetch user %s from %s",
                      m.second, infinit::beyond(true));
          }

        // Push all users
        for (auto const& u: res)
        {
          auto user_name = u.second.name;
          ELLE_TRACE("Pushing user %s (%s)", user_name, u.first);
          auto passport = infinit::model::doughnut::Passport(
            u.second.public_key,
            network.name,
            infinit::cryptography::rsa::KeyPair(owner.public_key,
                                                owner.private_key.get()),
            owner.public_key != network.owner,
            !deny_write,
            !deny_storage,
            false);
          try
          {
            ifnt.beyond_push(
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
          {
            char buf[4096];
            int r = getxattr(mountpoint,
                             elle::sprintf("infinit.resolve.%s", user_name),
                             buf, 4095, true);
            if (r > 0)
              {
                elle::printf("User \"%s\" already registerd to network.", user_name)
                  << std::endl;
                continue;
              }
          }
          setxattr(mountpoint, "infinit.register." + user_name,
                   passport_ser.string(), true);
          elle::printf("Registered user \"%s\" to network.", user_name)
            << std::endl;
        }
        // Push groups
        for (auto const& g: groups)
        {
          ELLE_TRACE_SCOPE("Creating group %s", g.first);
          char buf[4096];
          int r = getxattr(mountpoint,
                           elle::sprintf("infinit.resolve.%s", g.first),
                           buf, 4095, true);
          if (r > 0)
          {
            elle::printf("Group \"%s\" already exists on network.", g.first)
              << std::endl;
          }
          else
          {
            setxattr(mountpoint, "infinit.group.create", g.first, true);
            elle::printf("Added group \"%s\" to network.", g.first)
              << std::endl;
          }
          for (auto const& m: g.second)
          {
            if (res.find(m) == res.end())
            {
              ELLE_TRACE("skipping user %s", m);
              continue;
            }
            ELLE_TRACE("adding user %s", m);
            setxattr(mountpoint, "infinit.group.add",
                     g.first + ':' + res.at(m).name, true);
            elle::printf("Added user \"%s\" to group \"%s\" on network.",
                         m, g.first)
              << std::endl;
          }
        }
        return res;
      }
    }

    /*---------------------.
    | Mode: drive invite.  |
    `---------------------*/

    void
    LDAP::mode_drive_invite(std::string const& server,
                            std::string const& domain,
                            std::string const& user,
                            boost::optional<std::string> const& password,

                            std::string const& drive_name,
                            std::string const& root_permissions,
                            bool create_home,

                            std::string const& searchbase,
                            boost::optional<std::string> const& filter,
                            boost::optional<std::string> const& object_class,
                            std::string const& mountpoint_name,
                            bool deny_write,
                            bool deny_storage)
    {
      ELLE_TRACE_SCOPE("drive_invite");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      auto drive = ifnt.drive_get(ifnt.qualified_name(drive_name, owner));
      auto network = ifnt.network_descriptor_get(drive.network, owner);
      auto mode = mode_get(root_permissions);
      auto mountpoint = mountpoint_root(mountpoint_name, true);
      auto users = _populate_network(cli,
                                     server,
                                     domain,
                                     user,
                                     password,

                                     drive.network,

                                     searchbase,
                                     filter,
                                     object_class,
                                     mountpoint_name,
                                     deny_write,
                                     deny_storage);
      for (auto const& u: users)
      {
        auto const& user_name = u.second.name;
        auto set_permissions = [&user_name] (bfs::path const& folder,
                                             std::string const& perms) {
          setxattr(folder.string(),
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
            elle::printf("Created home directory: %s.", user_dir)
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
          ifnt.beyond_push(
            elle::sprintf("drives/%s/invitations/%s", drive.name, user_name),
            "invitation",
            elle::sprintf("%s: %s", drive.name, user_name),
            infinit::Drive::User(root_permissions, "ok", create_home),
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

    /*-------------------------.
    | Mode: populate network.  |
    `-------------------------*/

    void
    LDAP::mode_populate_network(std::string const& server,
                                std::string const& domain,
                                std::string const& user,
                                boost::optional<std::string> const& password,

                                std::string const& network_name,

                                std::string const& searchbase,
                                boost::optional<std::string> const& filter,
                                boost::optional<std::string> const& object_class,
                                std::string const& mountpoint,
                                bool deny_write,
                                bool deny_storage)
    {
      ELLE_TRACE_SCOPE("populate_network");
      auto& cli = this->cli();
      _populate_network(cli,
                        server,
                        domain,
                        user,
                        password,

                        network_name,

                        searchbase,
                        filter,
                        object_class,
                        mountpoint,
                        deny_write,
                        deny_storage);
    }
  }
}
