#include <infinit/cli/Drive.hh>


namespace infinit
{
  namespace cli
  {
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

    /*---------------.
    | Mode: create.  |
    `---------------*/

    void
    Drive::mode_create(std::string const& name,
                       boost::optional<std::string> const& description,
                       std::string const& network,
                       std::string const& volume,
                       boost::optional<std::string> const& icon,
                       bool push_drive,
                       bool push)
    {
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/
    void
    Drive::mode_delete(std::string const& name,
                       bool pull,
                       bool purge)
    {
    }


    /*---------------.
    | Mode: export.  |
    `---------------*/
    void
    Drive::mode_export(std::string const& name)
    {
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
