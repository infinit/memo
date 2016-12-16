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
  }
}
