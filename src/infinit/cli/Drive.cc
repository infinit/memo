#include <infinit/cli/Drive.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh> // infinit::beyond_delegate_user

ELLE_LOG_COMPONENT("infinit-drive");

namespace infinit
{
  namespace cli
  {
    Drive::Drive(Infinit& infinit)
      : Object(infinit)
      , create(*this,
               "Create a drive (a network and volume pair)",
               cli::name,
               cli::description = boost::none,
               cli::network,
               cli::volume,
               cli::icon = boost::none,
               cli::push_drive = false,
               cli::push = false)
      , delete_(*this,
                "Delete a drive locally",
                cli::name,
                cli::pull = false,
                cli::purge = false)
      , export_(*this,
                "Export a drive",
                cli::name)
      , fetch(*this,
              "Fetch drive from {hub}",
              elle::das::cli::Options(),
              cli::name = boost::none,
              cli::icon = boost::none)
      , invite(*this,
               "Invite a user to join the drive",
               cli::name,
               cli::user,
               cli::email,
               cli::fetch_drive = false,
               cli::fetch = false,
               cli::push_invitations = false,
               cli::push = false,
               cli::passport = false,
               // FIXME: should be hidden.
               cli::home = false)
      , join(*this,
             "Join a drive you were invited to (Hub operation)",
             elle::das::cli::Options(),
             cli::name)
      , list(*this, "List drives")
      , pull(*this,
             "Remove a drive from {hub}",
             elle::das::cli::Options(),
             cli::name,
             cli::purge = false)
      , push(*this,
             "Push a drive to {hub}",
             elle::das::cli::Options(),
             cli::name,
             cli::icon = boost::none)
    {}

    namespace
    {
      template <typename Buffer>
      void
      save_icon(infinit::cli::Infinit& cli,
                std::string const& name,
                Buffer const& buffer)
      {
        // XXX: move to infinit::icon_save maybe ?
        boost::filesystem::ofstream f;
        auto existed = cli.infinit()._open_write
          (f, cli.infinit()._drive_icon_path(name),
           name, "icon", true, std::ios::out | std::ios::binary);
        f.write(reinterpret_cast<char const*>(buffer.contents()),
                buffer.size());
        cli.report_action(existed ? "updated" : "saved",
                          "icon for drive", name, "locally");
      }

      void
      upload_icon(infinit::cli::Infinit& cli,
                  infinit::User& self,
                  infinit::Drive& drive,
                  boost::filesystem::path const& icon_path)
      {
        auto icon = [&]
          {
            boost::filesystem::ifstream icon;
            cli.infinit()._open_read(icon, icon_path, self.name, "icon");
            return std::string(std::istreambuf_iterator<char>{icon},
                               std::istreambuf_iterator<char>{});
          }();
        auto data = elle::ConstWeakBuffer(icon);
        auto url = elle::sprintf("drives/%s/icon", drive.name);
        cli.infinit()
          .beyond_push_data(url, "icon", drive.name, data, "image/jpeg", self);
        save_icon(cli, drive.name, data);
      }

      void
      pull_icon(infinit::cli::Infinit& cli,
                infinit::User& self,
                infinit::Drive& drive)
      {
        auto url = elle::sprintf("drives/%s/icon", drive.name);
        cli.infinit().beyond_delete(url, "icon", drive.name, self);
      }

      void
      do_push(infinit::cli::Infinit& cli,
              infinit::User& user,
              infinit::Drive& drive,
              boost::optional<std::string> const& icon_path)
      {
        if (icon_path
            && !icon_path->empty()
            && !boost::filesystem::exists(*icon_path))
          elle::err<CLIError>("%s doesn't exist", *icon_path);
        auto url = elle::sprintf("drives/%s", drive.name);
        cli.infinit().beyond_push(url, "drive", drive.name, drive, user);
        if (icon_path)
        {
          if (!icon_path->empty())
            upload_icon(cli, user, drive, *icon_path);
          else
            pull_icon(cli, user, drive);
        }
      }
    }

    /*---------------.
    | Mode: create.  |
    `---------------*/

    void
    Drive::mode_create(std::string const& name,
                       boost::optional<std::string> const& description,
                       std::string const& network_,
                       std::string const& volume_,
                       boost::optional<std::string> const& icon,
                       bool push_drive,
                       bool push)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      auto network = ifnt.network_get(network_, owner);
      auto volume_name = ifnt.qualified_name(volume_, owner);
      auto volume = infinit::Volume(ifnt.volume_get(volume_name));
      auto users = infinit::Drive::Users{};
      auto drive = infinit::Drive{drive_name, owner, volume, network,
                                  description, users};
      ifnt.drive_save(drive);
      if (push || push_drive)
        do_push(cli, owner, drive, icon);
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/

    void
    Drive::mode_delete(std::string const& name,
                       bool pull,
                       bool purge)
    {
      ELLE_TRACE_SCOPE("delete");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      if (pull)
        ifnt.beyond_delete("drive", drive_name, owner, true);
      if (purge)
      { /* Nothing depends on a drive. */ }
      auto drive = ifnt.drive_get(drive_name);
      ifnt.drive_delete(drive);
    }


    /*---------------.
    | Mode: export.  |
    `---------------*/

    namespace
    {
      boost::optional<boost::filesystem::path>
      icon_path(infinit::cli::Infinit& cli,
                std::string const& name)
      {
        auto path = cli.infinit()._drive_icon_path(name);
        if (boost::filesystem::exists(path))
          return path;
        else
          return {};
      }
    }

    void
    Drive::mode_export(std::string const& name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      // FIXME: there is no explicit path to pass to get_output?
      auto output = cli.get_output();
      auto drive_name = ifnt.qualified_name(name, owner);
      auto drive = ifnt.drive_get(drive_name);
      if (auto icon = icon_path(cli, name))
        drive.icon_path = icon->string();
      elle::serialization::json::serialize(drive, *output, false);
      cli.report_exported(*output, "drive", drive.name);
    }


    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    namespace
    {
      void
      fetch_(infinit::cli::Infinit& cli,
             std::string const& drive_name)
      {
        ELLE_TRACE_SCOPE("fetch %s", drive_name);
        auto remote_drive = cli.infinit().drive_fetch(drive_name);
        ELLE_DEBUG("save drive %s", remote_drive)
          cli.infinit().drive_save(remote_drive);
      }

      void
      fetch_icon(infinit::cli::Infinit& cli,
                 std::string const& name)
      {
        auto url = elle::sprintf("drives/%s/icon", name);
        auto request = cli.infinit().beyond_fetch_data(url, "icon", name);
        if (request->status() == elle::reactor::http::StatusCode::OK)
        {
          auto response = request->response();
          // XXX: Deserialize XML.
          if (response.size() == 0 || response[0] == '<')
            elle::err<infinit::MissingResource>(
                "icon for %s not found on %s", name, infinit::beyond(true));
          save_icon(cli, name, response);
        }
      }
    }

    void
    Drive::mode_fetch(boost::optional<std::string> const& name,
                      boost::optional<std::string> const& icon)
    {
      ELLE_TRACE_SCOPE("fetch");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      if (name)
      {
        ELLE_DEBUG("fetch specific drive");
        auto drive_name = ifnt.qualified_name(*name, owner);
        fetch_(cli, drive_name);
      }
      else
      {
        ELLE_DEBUG("fetch all drives");
        using Drives
          = std::unordered_map<std::string, std::vector<infinit::Drive>>;
        auto res = ifnt.beyond_fetch<Drives>
          (elle::sprintf("users/%s/drives", owner.name),
           "drives for user",
           owner.name,
           owner);
        for (auto const& drive: res["drives"])
          ifnt.drive_save(drive);
      }
      if (icon)
      {
        ELLE_DEBUG("fetch specific icon");
        fetch_icon(cli, *name);
      }
    }

    /*---------------.
    | Mode: invite.  |
    `---------------*/

    namespace
    {
      using Passport = Drive::Passport;

      void
      do_push_passport(infinit::cli::Infinit& cli,
                       infinit::Network const& network,
                       Passport const& passport,
                       infinit::User const& user,
                       infinit::User const& owner)
      {
        auto url = elle::sprintf("networks/%s/passports/%s",
                                 network.name, user.name);
        cli.infinit()
          .beyond_push(url,
                       "passport",
                       elle::sprintf("%s: %s", network.name, user.name),
                       passport,
                       owner);
      }

      void
      create_passport(infinit::cli::Infinit& cli,
                      infinit::User const& user,
                      infinit::Network const& network,
                      infinit::User const& owner,
                      bool push)
      {
        ELLE_TRACE_SCOPE("create_passport");
        try
        {
          auto passport = cli.infinit().passport_get(network.name, user.name);
          ELLE_DEBUG("passport (%s: %s) found", network.name, user.name);
          if (push)
            do_push_passport(cli, network, passport, user, owner);
        }
        catch (infinit::MissingResource const& e)
        {
          auto passport = Passport(
            user.public_key,
            network.name,
            elle::cryptography::rsa::KeyPair(owner.public_key,
                                                owner.private_key.get()));
          ELLE_DEBUG("passport (%s: %s) created", network.name, user.name);
          cli.infinit().passport_save(passport);
          if (push)
            do_push_passport(cli, network, passport, user, owner);
        }
      }

      using Invitations
        = std::unordered_map<std::string, infinit::Drive::User>;

      /// Compare the current drive json's invitee node with argument
      /// invitations.  Add non-existing users.
      void
      update_local_json(infinit::cli::Infinit& cli,
                        infinit::Drive& drive,
                        Invitations const& invitations)
      {
        for (auto const& invitation: invitations)
        {
          if (drive.owner == invitation.first)
            continue;

          auto it = drive.users.find(invitation.first);
          if (it != drive.users.end())
            continue;
          drive.users[invitation.first] = invitation.second;
          cli.report_action(
            "created", "invitation",
            elle::sprintf("%s: %s", drive.name, invitation.first));
        }
        cli.infinit().drive_save(drive);
      }

      void
      not_found(std::string const& name,
                std::string const& type)
      {
        elle::fprintf(std::cerr,
                      "%s %s not found on %s, ensure it has been pushed\n",
                      type, name, infinit::beyond(true));
      }
    }

    void
    Drive::mode_invite(std::string const& name,
                       std::vector<std::string> const& users,
                       std::vector<std::string> const& emails,
                       bool fetch_drive,
                       bool fetch,
                       bool push_invitations,
                       bool push,
                       bool generate_passports,
                       bool home)
    {
      ELLE_TRACE_SCOPE("invite");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      fetch |= fetch_drive;
      push |= push_invitations;
      ELLE_DEBUG("push: %s", push);
      if (emails.empty() && users.empty() && !push)
        elle::err<CLIError>("specify users using --user and/or --email");
      ELLE_DEBUG("generate passports: %s", generate_passports);
      for (auto const& email: emails)
        validate_email(email);

      if (fetch)
      {
        try
        {
          auto drive = ifnt.drive_fetch(drive_name);
          ifnt.drive_save(drive, true);
        }
        catch (infinit::MissingResource const& e)
        {
          if (e.what() != std::string("drive/not_found"))
            throw;
          // The drive has not been pushed yet. No need to sync.
        }
      }

      auto drive = ifnt.drive_get(drive_name);
      auto volume = ifnt.volume_get(drive.volume);
      auto network = ifnt.network_get(drive.network, owner);
      auto permissions = volume.default_permissions.value_or("none");

      auto invitees = Invitations{};
      if (users.empty() && emails.empty())
        for (auto const& u: drive.users)
          invitees[u.first] = u.second;
      for (auto const& user_name: users)
        invitees[user_name] = {permissions, "pending", home};
      if (!emails.empty())
      {
        // Ensure that the user has the passport for Beyond.
        static const std::string error_msg = elle::sprintf(
          "ERROR: In order to invite users by email, you must create"
          " a passport for \"%s\"\n"
          "with the --allow-create-passport option.\n"
          "You must then add the user to the DHT using"
          " infinit-acl --register\n\n",
          infinit::beyond_delegate_user());
        try
        {
          auto delegate_passport =
            ifnt.passport_get(network.name, infinit::beyond_delegate_user());
          if (!delegate_passport.allow_sign())
          {
            elle::fprintf(std::cerr, error_msg);
            elle::err("Missing --allow-create-passport flag for %s",
                      infinit::beyond_delegate_user());
          }
        }
        catch (infinit::MissingResource const& e)
        {
          elle::fprintf(std::cerr, error_msg);
          throw;
        }
        for (auto const& email: emails)
          invitees[email] = {permissions, "pending", home};
      }

      for (auto const& invitee: invitees)
      {
        // Email invitees do not need passports.
        if (validate_email(invitee.first))
          continue;
        auto user = ifnt.user_get(invitee.first);
        if (generate_passports)
          create_passport(cli, user, network, owner, push);
        else
        {
          // Ensure that the user has a passport.
          auto passport = ifnt.passport_get(network.name, user.name);
          if (push)
            do_push_passport(cli, network, passport, user, owner);
        }
      }

      if (!users.empty() || !emails.empty())
        update_local_json(cli, drive, invitees);

      if (push)
      {
        auto url = elle::sprintf("drives/%s/invitations", drive.name);
        for (auto const& invitee: invitees)
        {
          try
          {
            cli.infinit()
              .beyond_push(
              elle::sprintf("drives/%s/invitations/%s",drive.name,
                            invitee.first),
              "invitation",
              elle::sprintf("%s: %s", drive.name, invitee.first),
              drive.users[invitee.first],
              owner,
              true,
              true);
          }
          catch (infinit::BeyondError const& e)
          {
            auto type = [] (std::string const& err) -> std::string
              {
                static const auto map
                = std::unordered_map<std::string, std::string>
                {
                  {"drive/not_found", "Drive"},
                  {"network/not_found", "Network"},
                  {"passport/not_found", "Passport"},
                  {"user/not_found", "User"},
                  {"volume/not_found", "Volume"},
                };
                auto i = map.find(err);
                if (i == map.cend())
                  return "";
                else
                  return i->second;
              }(e.error());
            if (!type.empty())
              not_found(e.name_opt(), type);

            throw;
          }
        }
      }
    }


    /*-------------.
    | Mode: join.  |
    `-------------*/
    void
    Drive::mode_join(std::string const& name)
    {
      ELLE_TRACE_SCOPE("join");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      auto drive = ifnt.drive_get(drive_name);
      if (owner.name == boost::filesystem::path(drive.name).parent_path().string())
        elle::err("The owner is automatically invited to its drives");
      auto it = drive.users.find(owner.name);
      if (it == drive.users.end())
        elle::err("You haven't been invited to join %s", drive.name);
      auto invitation = it->second;
      invitation.status = "ok";
      auto url = elle::sprintf("drives/%s/invitations/%s",
                               drive.name, owner.name);
      try
      {
        cli.infinit()
          .beyond_push(url, "invitation", drive.name, invitation, owner, false);
        cli.report_action("joined", "drive", drive.name);
      }
      catch (infinit::MissingResource const& e)
      {
        auto err = std::string{e.what()};
        if (err == "user/not_found")
          not_found(owner.name, "User"); // XXX: It might be the owner or you.
        else if (err == "drive/not_found")
          not_found(drive.name, "Drive");

        throw;
      }
      drive.users[owner.name] = invitation;
      ELLE_DEBUG("save drive %s", drive)
        ifnt.drive_save(drive);
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    void
    Drive::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      if (cli.script())
      {
        auto l = elle::json::Array{};
        for (auto& drive: ifnt.drives_get())
        {
          auto o = elle::json::Object{};
          o["name"] = static_cast<std::string>(drive.name);
          if (drive.users.find(owner.name) != drive.users.end())
            o["status"] = drive.users[owner.name].status;
          if (drive.description)
            o["description"] = drive.description.get();
          l.emplace_back(std::move(o));
        }
        elle::json::write(std::cout, l);
      }
      else
        for (auto& drive: ifnt.drives_get())
        {
          std::cout << drive.name;
          if (drive.description)
            std::cout << " \"" << drive.description.get() << "\"";
          if (drive.users.find(owner.name) != drive.users.end())
            std::cout << ": " << drive.users[owner.name].status;
          std::cout << std::endl;
        }
    }


    /*-------------.
    | Mode: pull.  |
    `-------------*/

    void
    Drive::mode_pull(std::string const& name,
                     bool purge)
    {
      ELLE_TRACE_SCOPE("pull");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      ifnt.beyond_delete("drive", drive_name, owner, false, purge);
    }


    /*-------------.
    | Mode: push.  |
    `-------------*/

    void
    Drive::mode_push(std::string const& name,
                     boost::optional<std::string> const& icon)
    {
      ELLE_TRACE_SCOPE("push");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto drive_name = ifnt.qualified_name(name, owner);
      auto drive = ifnt.drive_get(drive_name);
      do_push(cli, owner, drive, icon);
    }
  }
}
