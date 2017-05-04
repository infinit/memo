#include <infinit/cli/Journal.hh>

#include <elle/bytes.hh>
#include <elle/printf.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("cli.journal");

namespace bfs = boost::filesystem;

namespace infinit
{
  namespace cli
  {
    using Error = elle::das::cli::Error;

    using Async = infinit::model::doughnut::consensus::Async;

    Journal::Journal(Infinit& infinit)
      : Object(infinit)
      , describe(*this,
                 "Describe asynchronous operation(s)",
                 cli::network,
                 cli::operation = boost::none)
      , export_(*this,
                "Export an operation",
                cli::network,
                cli::operation)
      , stat(*this,
             "Show the remaining asynchronous operations count and size",
             cli::network = boost::none)
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
                    bfs::path const& path, std::string const& id)
      {
        bfs::ifstream f;
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
      ELLE_TRACE_SCOPE("describe");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_get(network_name, owner);
      auto dht = network.run(owner);
      bfs::path async_path = network.cache_dir(owner) / "async";
      auto report = [&] (bfs::path const& path)
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
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_get(network_name, owner);
      auto id = std::to_string(operation);
      auto path = network.cache_dir(owner) / "async" / id;
      auto op = get_operation(ifnt, owner, network, path, id);
      elle::serialization::json::serialize(op, std::cout);
    }

    /*-------------.
    | Mode: stat.  |
    `-------------*/

    void
    Journal::mode_stat(boost::optional<std::string> const& network_name)
    {
      ELLE_TRACE_SCOPE("stat");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto networks = std::vector<infinit::Network>{};
      if (network_name)
        networks.emplace_back(ifnt.network_get(*network_name, owner));
      else
        networks = ifnt.networks_get(owner);
      auto res = elle::json::Object{};
      for (auto const& network: networks)
      {
        bfs::path async_path = network.cache_dir(owner) / "async";
        int operation_count = 0;
        int64_t data_size = 0;
        if (bfs::exists(async_path))
          for (auto const& p: bfs::directory_iterator(async_path))
            if (is_visible_file(p))
            {
              operation_count++;
              data_size += bfs::file_size(p);
            }
        if (cli.script())
          res[network.name] = elle::json::Object
            {
              {"operations", operation_count},
              {"size", data_size},
            };
        else
          elle::fprintf(std::cout,
                        "%s: %s operations, %s\n",
                        network.name, operation_count,
                        elle::human_data_size(data_size));
      }
      if (cli.script())
        elle::json::write(std::cout, res);
    }
  }
}
