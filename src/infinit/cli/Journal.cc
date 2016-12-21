#include <infinit/cli/Journal.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("cli.journal");

namespace fs = boost::filesystem;

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    using Async = infinit::model::doughnut::consensus::Async;

    Journal::Journal(Infinit& infinit)
      : Entity(infinit)
      , describe(
        "Describe asynchronous operation(s)",
        das::cli::Options(),
        this->bind(modes::mode_describe,
                   cli::network,
                   cli::operation = boost::none))
      , export_(
        "Export an operation",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::network,
                   cli::operation))
      , stat(
        "Show the remaining asynchronous operations count and size",
        das::cli::Options(),
        this->bind(modes::mode_stat,
                   cli::network = boost::none))
    {}

    /*-----------------.
    | Mode: describe.  |
    `-----------------*/

    namespace
    {
      Async::Op
      get_operation(infinit::Infinit& ifnt,
                    infinit::User const& owner,
                    infinit::Network& network,
                    fs::path const& path, std::string const& id)
      {
        fs::ifstream f;
        ifnt._open_read(f, path, id, "operation");
        auto dht = network.run(owner);
        auto ctx = elle::serialization::Context
          {
            dht.get(),
            infinit::model::doughnut::ACBDontWaitForSignature{},
            infinit::model::doughnut::OKBDontWaitForSignature{}
          };
        return elle::serialization::binary::deserialize<Async::Op>(f, true, ctx);
      }
    }

    void
    Journal::mode_describe(std::string const& network_name,
                           boost::optional<int> operation)
    {
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_get(network_name, owner);
      auto dht = network.run(owner);
      fs::path async_path = network.cache_dir(owner) / "async";
      auto report = [&] (fs::path const& path)
        {
          auto name = path.filename().string();
          std::cout << name << ": ";
          try
          {
            auto op = get_operation(ifnt, owner, network, path, name);
            if (op.resolver)
              std::cout << op.resolver->description();
            else
              std::cout << "no description for this operation";
          }
          catch (elle::serialization::Error const&)
          {
            std::cerr << "error: " << elle::exception_string();
          }
          std::cout << std::endl;
        };
      if (operation)
        report(async_path / elle::sprintf("%s", *operation));
      else
        for (auto const& path: Async::entries(async_path))
          report(path);
    }

    /*---------------.
    | Mode: export.  |
    `---------------*/

    void
    Journal::mode_export(std::string const& network_name,
                         int operation)
    {
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_get(network_name, owner);
      auto id = std::to_string(operation);
      auto path = network.cache_dir(owner) / "async" / id;
      auto op = get_operation(ifnt, owner, network, path, id);
      elle::serialization::json::serialize(op, std::cout);
    }

    /*--------------.
    | Mode: stats.  |
    `--------------*/

    void
    Journal::mode_stat(boost::optional<std::string> const& network)
    {}
  }
}
