#include <infinit/cli/Network.hh>

#include <boost/tokenizer.hpp>

#include <elle/make-vector.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/MonitoringServer.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Configuration.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/storage/Strip.hh>

ELLE_LOG_COMPONENT("cli.network");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Network::Network(Infinit& infinit)
      : Entity(infinit)
      , create(
        "Create a network",
        das::cli::Options(),
        this->bind(modes::mode_create,
                   cli::name,
                   cli::description = boost::none,
                   cli::storage = Strings{},
                   cli::port = boost::none,
                   cli::replication_factor = 1,
                   cli::eviction_delay = boost::none,
                   cli::output = boost::none,
                   cli::push_network = false,
                   cli::push = false,
                   cli::admin_r = Strings{},
                   cli::admin_rw = Strings{},
                   cli::peer = Strings{},
                   // Consensus types.
                   cli::paxos = false,
                   cli::no_consensus = false,
                   // Overlay types.
                   cli::kelips = false,
                   cli::kalimero = false,
                   cli::kouncil = false,
                   // Kelips options,
                   cli::nodes = boost::none,
                   cli::k = boost::none,
                   cli::kelips_contact_timeout = boost::none,
                   cli::encrypt = boost::none,
                   cli::protocol = boost::none))
      , export_(
        "Export a network",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::name,
                   cli::output = boost::none))
    {}


    /*---------------.
    | Mode: create.  |
    `---------------*/

    namespace
    {
      infinit::model::doughnut::Protocol
      protocol_get(std::string const& proto)
      {
        try
        {
          return elle::serialization::Serialize<
            infinit::model::doughnut::Protocol>::convert(proto);
        }
        catch (elle::serialization::Error const& e)
        {
          elle::err<Error>("'protocol' must be 'utp', 'tcp' or 'all': %s",
                           proto);
        }
      }

      auto
      make_kelips_config(boost::optional<int> nodes,
                         boost::optional<int> k,
                         boost::optional<std::string> timeout,
                         boost::optional<std::string> encrypt,
                         boost::optional<std::string> protocol)
      {
        auto res = std::make_unique<infinit::overlay::kelips::Configuration>();
        // k.
        if (k)
          res->k = *k;
        else if (nodes)
        {
          if (*nodes < 10)
            res->k = 1;
          else if (sqrt(*nodes) < 5)
            res->k = *nodes / 5;
          else
            res->k = sqrt(*nodes);
        }
        else
          res->k = 1;
        if (timeout)
          res->contact_timeout_ms =
            std::chrono::duration_from_string<std::chrono::milliseconds>(*timeout)
            .count();
        if (encrypt)
        {
          std::string enc = *encrypt;
          if (enc == "no")
          {
            res->encrypt = false;
            res->accept_plain = true;
          }
          else if (enc == "lazy")
          {
            res->encrypt = true;
            res->accept_plain = true;
          }
          else if (enc == "yes")
          {
            res->encrypt = true;
            res->accept_plain = false;
          }
          else
            elle::err<Error>("'encrypt' must be 'no', 'lazy' or 'yes': %s", enc);
        }
        else
        {
          res->encrypt = true;
          res->accept_plain = false;
        }
        if (protocol)
          res->rpc_protocol = protocol_get(*protocol);
        return res;
      }

      std::vector<infinit::model::Endpoints>
      parse_peers(std::vector<std::string> const& speers)
      {
        using tokenizer = boost::tokenizer<boost::char_separator<char>>;
        return elle::make_vector(speers, [&](auto const& s)
          {
            auto sep = boost::char_separator<char>{","};
            auto res = infinit::model::Endpoints{};
            try
            {
              for (auto const& s: tokenizer{s, sep})
                res.emplace_back(s);
            }
            catch (elle::Error const& e)
            {
              elle::err("Malformed endpoints '%s': %s", s, e);
            }
            return res;
          });
      }

      std::unique_ptr<infinit::storage::StorageConfig>
      storage_configuration(infinit::Infinit& ifnt,
                            std::vector<std::string> const& storage)
      {
        auto res = std::unique_ptr<infinit::storage::StorageConfig>{};
        if (storage.empty())
          return {};
        else
        {
          auto backends
            = elle::make_vector(storage,
                                [&](auto const& s) { return ifnt.storage_get(s); });
          if (backends.size() == 1)
            return std::move(backends[0]);
          else
            return std::make_unique<infinit::storage::StripStorageConfig>
              (std::move(backends));
        }
      }
    }

    void
    Network::mode_create(std::string const& network_name,
                         boost::optional<std::string> description,
                         std::vector<std::string> const& storage_names,
                         boost::optional<int> port,
                         int replication_factor,
                         boost::optional<std::string> eviction_delay,
                         boost::optional<std::string> const& output_name,
                         bool push_network,
                         bool push,
                         std::vector<std::string> const& admin_r,
                         std::vector<std::string> const& admin_rw,
                         std::vector<std::string> const& peer,
                         // Consensus types.
                         bool paxos,
                         bool no_consensus,
                         // Overlay types.
                         bool kelips,
                         bool kalimero,
                         bool kouncil,
                         // Kelips options,
                         boost::optional<int> nodes,
                         boost::optional<int> k,
                         boost::optional<std::string> kelips_contact_timeout,
                         boost::optional<std::string> encrypt,
                         boost::optional<std::string> protocol)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      auto overlay_config = [&]() -> std::unique_ptr<infinit::overlay::Configuration>
        {
          if (1 < kalimero + kelips + kouncil)
            elle::err<Error>("only one overlay type must be specified");
          if (kalimero)
            return std::make_unique<infinit::overlay::KalimeroConfiguration>();
          else if (kouncil)
            return std::make_unique<infinit::overlay::kouncil::Configuration>();
          else
            return make_kelips_config(nodes, k, kelips_contact_timeout,
                                      encrypt, protocol);
        }();

      auto storage = storage_configuration(ifnt, storage_names);
      // Consensus
      auto consensus_config =
        std::unique_ptr<infinit::model::doughnut::consensus::Configuration>{};
      {
        if (replication_factor < 1)
          elle::err<Error>("replication factor must be greater than 0");
        if (!no_consensus)
          paxos = true;
        if (1 < no_consensus + paxos)
          elle::err<Error>("more than one consensus specified");
        if (paxos)
        {
          consensus_config = std::make_unique<
            infinit::model::doughnut::consensus::Paxos::Configuration>(
              replication_factor,
              eviction_delay ?
              std::chrono::duration_from_string<std::chrono::seconds>(*eviction_delay) :
              std::chrono::seconds(10 * 60));
        }
        else
        {
          if (replication_factor != 1)
            elle::err("without consensus, replication factor must be 1");
          consensus_config = std::make_unique<
            infinit::model::doughnut::consensus::Configuration>();
        }
      }
      infinit::model::doughnut::AdminKeys admin_keys;
      auto add_admin =
        [&admin_keys] (infinit::cryptography::rsa::PublicKey const& key,
                       bool read, bool write)
        {
          if (read && !write)
          {
            auto& target = admin_keys.r;
            if (std::find(target.begin(), target.end(), key) == target.end())
              target.push_back(key);
          }
          if (write) // Implies RW.
          {
            auto& target = admin_keys.w;
            if (std::find(target.begin(), target.end(), key) == target.end())
              target.push_back(key);
          }
        };
      for (auto const& a: admin_r)
        add_admin(ifnt.user_get(a).public_key, true, false);
      for (auto const& a: admin_rw)
        add_admin(ifnt.user_get(a).public_key, true, true);

      auto peers = std::vector<infinit::model::Endpoints>{};
      if (!peer.empty())
        peers = parse_peers(peer);

      auto dht =
        std::make_unique<infinit::model::doughnut::Configuration>(
          infinit::model::Address::random(0), // FIXME
          std::move(consensus_config),
          std::move(overlay_config),
          std::move(storage),
          owner.keypair(),
          std::make_shared<infinit::cryptography::rsa::PublicKey>(owner.public_key),
          infinit::model::doughnut::Passport(
            owner.public_key,
            ifnt.qualified_name(network_name, owner),
            infinit::cryptography::rsa::KeyPair(owner.public_key,
                                                owner.private_key.get())),
          owner.name,
          std::move(port),
          infinit::version(),
          admin_keys,
          peers);
      {
        auto network = infinit::Network(ifnt.qualified_name(network_name, owner),
                                        std::move(dht),
                                        description);
        auto desc = std::unique_ptr<infinit::NetworkDescriptor>{};
        if (output_name)
        {
          auto output = cli.get_output(output_name);
          elle::serialization::json::serialize(network, *output, false);
          desc.reset(new infinit::NetworkDescriptor(std::move(network)));
        }
        else
        {
          ifnt.network_save(owner, network);
          desc.reset(new infinit::NetworkDescriptor(std::move(network)));
          ifnt.network_save(*desc);
          cli.report_created("network", desc->name);
        }
        if (push || push_network)
          ifnt.beyond_push("network", desc->name, *desc, owner);
      }
    }


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
