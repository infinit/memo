#include <infinit/cli/Drive.hh>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("infinit-drive");

namespace infinit
{
  namespace cli
  {
    /// Command line errors.
    using Error = das::cli::Error;

    Drive::Drive(Infinit& infinit)
      : Entity(infinit)
      , create(
        "Create a drive (a network and volume pair)",
        das::cli::Options(),
        this->bind(modes::mode_create,
                   cli::name,
                   cli::description = boost::none,
                   cli::network,
                   cli::volume,
                   cli::icon = boost::none,
                   cli::push_drive = false,
                   cli::push = false))
      , delete_(
        "Delete a drive locally",
        das::cli::Options(),
        this->bind(modes::mode_delete,
                   cli::name,
                   cli::pull = false,
                   cli::purge = false))
      , export_(
        "Export a drive",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::name))
      , fetch(
        "Fetch drive from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   cli::name = boost::none,
                   cli::icon = boost::none))
      , invite(
        "Invite a user to join the drive",
        das::cli::Options(),
        this->bind(modes::mode_invite,
                   cli::name,
                   cli::user,
                   cli::email,
                   cli::fetch_drive = false,
                   cli::fetch = false,
                   cli::push_invitations = false,
                   cli::push = false,
                   cli::passport = false,
                   // FIXME: should be hidden.
                   cli::permissions = boost::none,
                   cli::home = false))
      , join(
        "Join a drive you were invited to (Hub operation)",
        das::cli::Options(),
        this->bind(modes::mode_join,
                   cli::name))
      , list(
        "List drives",
        das::cli::Options(),
        this->bind(modes::mode_list))
      , pull(
        "Remove a drive from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_pull,
                   cli::name,
                   cli::purge = false))
      , push(
        "Push a drive to {hub}",
        das::cli::Options(),
        this->bind(modes::mode_push,
                   cli::name))
    {}

    namespace
    {
      template <typename Buffer>
      void
      save_icon(infinit::cli::Infinit& cli,
                std::string const& name,
                Buffer const& buffer)
      {
        boost::filesystem::ofstream f;
        cli.infinit()._open_write
          (f, cli.infinit()._drive_icon_path(name),
           name, "icon", true, std::ios::out | std::ios::binary);
        f.write(reinterpret_cast<char const*>(buffer.contents()),
                buffer.size());
        cli.report_action("fetched", "icon", name, "locally");
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
          elle::err<Error>("%s doesn't exist", *icon_path);
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
      cli.report_action("created", "drive", drive.name, "locally");
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
      auto path = ifnt._drive_path(drive_name);
      if (pull)
        ifnt.beyond_delete("drive", drive_name, owner, true);
      if (purge)
        { /* Nothing depends on a drive. */ }
      if (boost::filesystem::remove(path))
        cli.report_action("deleted", "drive", name, "locally");
      else
        elle::err<infinit::MissingLocalResource>
          ("File for drive could not be deleted: %s", path);
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
    void
    Drive::mode_fetch(boost::optional<std::string> const& name,
                      boost::optional<std::string> const& icon)
    {
    }

    /*---------------.
    | Mode: invite.  |
    `---------------*/

    void
    Drive::mode_invite(std::string const& name,
                       std::vector<std::string> const& user,
                       std::vector<std::string> const& email,
                       bool fetch_drive,
                       bool fetch,
                       bool push_invitations,
                       bool push,
                       bool passport,
                       boost::optional<std::string> const& permissions,
                       bool home)
    {
    }


    /*-------------.
    | Mode: join.  |
    `-------------*/
    void
    Drive::mode_join(std::string const& name)
    {
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/
    void
    Drive::mode_list()
    {
    }



    /*-------------.
    | Mode: pull.  |
    `-------------*/
    void
    Drive::mode_pull(std::string const& name,
                     bool purge)
    {
    }


    /*-------------.
    | Mode: push.  |
    `-------------*/
    void
    Drive::mode_push(std::string const& name)
    {
    }
  }
}
