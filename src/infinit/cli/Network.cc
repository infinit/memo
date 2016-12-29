#include <infinit/cli/Network.hh>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("cli.network");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Network::Network(Infinit& infinit)
      : Entity(infinit)
      , export_(
        "Export a network",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::name,
                   cli::output = boost::none))
    {}


    /*---------------.
    | Mode: export.  |
    `---------------*/

    void
    Network::mode_export(std::string const& network_name,
                         boost::optional<std::string> const& output_name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(network_name, owner);
      auto desc = ifnt.network_descriptor_get(network_name, owner);
      auto output = cli.get_output(output_name);
      name = desc.name;
      elle::serialization::json::serialize(desc, *output, false);
      cli.report_exported(*output, "network", desc.name);
    }
  }
}
