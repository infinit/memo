#include <infinit/cli/Network.hh>

#include <boost/tokenizer.hpp>

#include <elle/make-vector.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/cli/xattrs.hh>
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
    using Strings = Network::Strings;

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
      , fetch(
        "Fetch a network from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   cli::name = boost::none))
      , import(
        "Fetch a network",
        das::cli::Options(),
        this->bind(modes::mode_import,
                   cli::input = boost::none))
      , link(
        "Link this device to a network",
        das::cli::Options(),
        this->bind(modes::mode_link,
                   cli::name,
                   cli::storage = Strings{},
                   cli::output = boost::none))
      , list(
        "List networks",
        das::cli::Options(),
        this->bind(modes::mode_list))
      , unlink(
        "Unlink this device from a network",
        das::cli::Options(),
        this->bind(modes::mode_unlink,
                   cli::name))
      , update(
        "Update a network",
        das::cli::Options(),
        this->bind(modes::mode_update,
                   cli::name,
                   cli::description = boost::none,
                   cli::port = boost::none,
                   cli::output = boost::none,
                   cli::push_network = false,
                   cli::push = false,
                   cli::admin_r = Strings{},
                   cli::admin_rw = Strings{},
                   cli::admin_remove = Strings{},
                   cli::mountpoint = boost::none,
                   cli::peer = Strings{}))
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
                         boost::optional<std::string> const& timeout,
                         boost::optional<std::string> const& encrypt,
                         boost::optional<std::string> const& protocol)
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
      parse_peers(Strings const& speers)
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
      make_storage_config(infinit::Infinit& ifnt,
                          Strings const& storage)
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

      // Consensus
      auto
      make_consensus_config(bool paxos,
                            bool no_consensus,
                            int replication_factor,
                            boost::optional<std::string> const& eviction_delay)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Configuration>
      {
        if (replication_factor < 1)
          elle::err<Error>("replication factor must be greater than 0");
        if (!no_consensus)
          paxos = true;
        if (1 < no_consensus + paxos)
          elle::err<Error>("more than one consensus specified");
        if (paxos)
          return std::make_unique<
            infinit::model::doughnut::consensus::Paxos::Configuration>(
              replication_factor,
              eviction_delay ?
              std::chrono::duration_from_string<std::chrono::seconds>(*eviction_delay) :
              std::chrono::seconds(10 * 60));
        else
        {
          if (replication_factor != 1)
            elle::err("without consensus, replication factor must be 1");
          return std::make_unique<
            infinit::model::doughnut::consensus::Configuration>();
        }
      }

      auto
      make_admin_keys(infinit::Infinit& ifnt,
                      Strings const& admin_r,
                      Strings const& admin_rw)
        -> infinit::model::doughnut::AdminKeys
      {
        auto res = infinit::model::doughnut::AdminKeys{};
        auto add =
          [&res] (infinit::cryptography::rsa::PublicKey const& key,
                  bool read, bool write)
          {
            if (read && !write)
              push_back_if_missing(res.r, key);
            // write implies rw.
            if (write)
              push_back_if_missing(res.w, key);
          };
        for (auto const& a: admin_r)
          add(ifnt.user_get(a).public_key, true, false);
        for (auto const& a: admin_rw)
          add(ifnt.user_get(a).public_key, true, true);
        return res;
      }
    }

    void
    Network::mode_create(std::string const& network_name,
                         boost::optional<std::string> const& description,
                         Strings const& storage_names,
                         boost::optional<int> port,
                         int replication_factor,
                         boost::optional<std::string> const& eviction_delay,
                         boost::optional<std::string> const& output_name,
                         bool push_network,
                         bool push,
                         Strings const& admin_r,
                         Strings const& admin_rw,
                         Strings const& peer,
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
                         boost::optional<std::string> const& kelips_contact_timeout,
                         boost::optional<std::string> const& encrypt,
                         boost::optional<std::string> const& protocol)
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

      auto storage = make_storage_config(ifnt, storage_names);
      auto consensus_config = make_consensus_config(paxos,
                                                    no_consensus,
                                                    replication_factor,
                                                    eviction_delay);
      auto admin_keys = make_admin_keys(ifnt, admin_r, admin_rw);

      auto peers = std::vector<infinit::model::Endpoints>{};
      if (!peer.empty())
        peers = parse_peers(peer);

      auto dht =
        std::make_unique<infinit::model::doughnut::Configuration>(
          infinit::model::Address::random(0),
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
        auto desc = [&] {
            if (output_name)
            {
              auto output = cli.get_output(output_name);
              elle::serialization::json::serialize(network, *output, false);
              return std::make_unique<infinit::NetworkDescriptor>(std::move(network));
            }
            else
            {
              ifnt.network_save(owner, network);
              auto res = std::make_unique<infinit::NetworkDescriptor>(std::move(network));
              ifnt.network_save(*res);
              cli.report_created("network", res->name);
              return res;
            }
          }();
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


    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    void
    Network::mode_fetch(boost::optional<std::string> const& network_name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      auto save = [&ifnt,&owner] (infinit::NetworkDescriptor desc_) {
        // Save or update network descriptor.
        ifnt.network_save(desc_, true);
        for (auto const& u: ifnt.network_linked_users(desc_.name))
        {
          // Copy network descriptor.
          auto desc = desc_;
          auto network = ifnt.network_get(desc.name, u, false);
          if (network.model)
          {
            auto* d = dynamic_cast<infinit::model::doughnut::Configuration*>(
              network.model.get()
            );
            auto updated_network = infinit::Network(
              desc.name,
              std::make_unique<infinit::model::doughnut::Configuration>(
                d->id,
                std::move(desc.consensus),
                std::move(desc.overlay),
                std::move(d->storage),
                u.keypair(),
                std::make_shared<infinit::cryptography::rsa::PublicKey>(
                  desc.owner),
                d->passport,
                u.name,
                d->port,
                desc.version,
                desc.admin_keys,
                desc.peers),
              desc.description);
            // Update linked network for user.
            ifnt.network_save(u, updated_network, true);
          }
        }
      };
      if (network_name)
      {
        auto name = ifnt.qualified_name(*network_name, owner);
        save(ifnt.beyond_fetch<infinit::NetworkDescriptor>("network", name));
      }
      else // Fetch all networks for owner.
      {
        using Networks
          = std::unordered_map<std::string, std::vector<infinit::NetworkDescriptor>>;
        auto res =
          ifnt.beyond_fetch<Networks>(
            elle::sprintf("users/%s/networks", owner.name),
            "networks for user",
            owner.name,
            owner);
        for (auto const& n: res["networks"])
          save(n);
      }
    }


    /*---------------.
    | Mode: import.  |
    `---------------*/
    void
    Network::mode_import(boost::optional<std::string> const& input_name)
    {
      ELLE_TRACE_SCOPE("import");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto input = this->cli().get_input(input_name);
      auto desc =
        elle::serialization::json::deserialize<infinit::NetworkDescriptor>
        (*input, false);
      ifnt.network_save(desc);
      cli.report_imported("network", desc.name);
    }


    /*-------------.
    | Mode: link.  |
    `-------------*/

    void
    Network::mode_link(std::string const& network_name,
                       Strings const& storage_names,
                       boost::optional<std::string> const& output_name)
    {
      ELLE_TRACE_SCOPE("link");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      {
        auto network = ifnt.network_get(network_name, owner, false);
        if (network.model)
          elle::err("%s is already linked with %s", network.name, owner.name);
      }

      auto storage = make_storage_config(ifnt, storage_names);
      auto desc = ifnt.network_descriptor_get(network_name, owner);
      auto passport = [&] () -> infinit::Infinit::Passport
        {
          if (owner.public_key == desc.owner)
            return {owner.public_key, desc.name,
                    infinit::cryptography::rsa::KeyPair(owner.public_key,
                                                        owner.private_key.get())};
          try
          {
            return ifnt.passport_get(desc.name, owner.name);
          }
          catch (infinit::MissingLocalResource const&)
          {
            elle::err("missing passport (%s: %s), "
                      "use infinit-passport to fetch or import",
                      desc.name, owner.name);
          }
        }();
      if (!passport.verify(passport.certifier() ? *passport.certifier() : desc.owner))
        elle::err("passport signature is invalid");
      if (storage && !passport.allow_storage())
        elle::err("passport does not allow storage");
      auto network = infinit::Network(
        desc.name,
        std::make_unique<infinit::model::doughnut::Configuration>(
          infinit::model::Address::random(0), // FIXME
          std::move(desc.consensus),
          std::move(desc.overlay),
          std::move(storage),
          owner.keypair(),
          std::make_shared<infinit::cryptography::rsa::PublicKey>(desc.owner),
          std::move(passport),
          owner.name,
          boost::optional<int>(),
          desc.version,
          desc.admin_keys,
          desc.peers),
        desc.description);
      if (output_name)
        ifnt.save(*cli.get_output(output_name), network, false);
      else
      {
        ifnt.network_save(owner, network, true);
        cli.report_action("linked", "device to network", desc.name);
      }
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    void
    Network::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      if (cli.script())
      {
        auto l = elle::json::Array{};
        for (auto const& network: ifnt.networks_get(owner))
        {
          auto o = elle::json::Object
            {
              {"name", static_cast<std::string>(network.name)},
              {"linked", bool(network.model) && network.user_linked(owner)},
            };
          if (network.description)
            o["description"] = network.description.get();
          l.emplace_back(std::move(o));
        }
        elle::json::write(std::cout, l);
      }
      else
      {
        for (auto const& network: ifnt.networks_get(owner))
        {
          std::cout << network.name;
          if (network.description)
            std::cout << " \"" << network.description.get() << "\"";
          if (network.model && network.user_linked(owner))
            std::cout << ": linked";
          else
            std::cout << ": not linked";
          std::cout << std::endl;
        }
      }
    }

    /*---------------.
    | Mode: unlink.  |
    `---------------*/

    void
    Network::mode_unlink(std::string const& network_name)
    {
      ELLE_TRACE_SCOPE("unlink");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto network = ifnt.network_get(network_name, owner, true);
      ifnt.network_unlink(network.name, owner);
      this->cli().report_action("unlinked", "network", network_name);
    }


    /*---------------.
    | Mode: update.  |
    `---------------*/

    namespace
    {
      std::pair<infinit::cryptography::rsa::PublicKey, bool>
      user_key(infinit::Infinit& ifnt,
               std::string name,
               boost::optional<std::string> const& mountpoint)
      {
        bool is_group = false;
        if (!name.empty() && name[0] == '@')
        {
          is_group = true;
          name = name.substr(1);
        }
        if (!name.empty() && name[0] == '{')
        {
          elle::Buffer buf(name);
          elle::IOStream is(buf.istreambuf());
          auto key = elle::serialization::json::deserialize
            <infinit::cryptography::rsa::PublicKey>(is);
          return std::make_pair(key, is_group);
        }
        if (!is_group)
          return std::make_pair(ifnt.user_get(name).public_key, false);
        if (!mountpoint)
          elle::err("A mountpoint is required to fetch groups.");
        char buf[32768];
        int res = getxattr(*mountpoint,
                           "infinit.group.control_key." + name,
                           buf, 16384, true);
        if (res <= 0)
          elle::err("Unable to fetch group %s", name);
        elle::Buffer b(buf, res);
        elle::IOStream is(b.istreambuf());
        auto key = elle::serialization::json::deserialize
          <infinit::cryptography::rsa::PublicKey>(is);
        return std::make_pair(key, is_group);
      }
    }

    void
    Network::mode_update(std::string const& network_name,
                         boost::optional<std::string> const& description,
                         boost::optional<int> port,
                         boost::optional<std::string> const& output_name,
                         bool push_network,
                         bool push,
                         Strings const& admin_r,
                         Strings const& admin_rw,
                         Strings const& admin_remove,
                         boost::optional<std::string> const& mountpoint,
                         Strings const& peer)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      auto network = ifnt.network_get(network_name, owner);
      if (description)
        network.description = description;
      network.ensure_allowed(owner, "update");
      auto& dht = *network.dht();
      if (port)
        dht.port = *port;
      if (cli.compatibility_version())
        dht.version = *cli.compatibility_version();
      bool changed_admins = false;
      auto check_group_mount = [&] (bool group)
        {
          if (group && !mountpoint)
            elle::err<Error>("must specify mountpoint of volume on "
                             "network \"%s\" to edit group admins",
                             network.name);
        };
      auto add_admin = [&] (infinit::cryptography::rsa::PublicKey const& key,
                            bool group, bool read, bool write)
        {
          if (read && !write)
            push_back_if_missing(group ? dht.admin_keys.group_r : dht.admin_keys.r,
                                 key);
          if (write) // Implies RW.
            push_back_if_missing(group ? dht.admin_keys.group_w : dht.admin_keys.w,
                                 key);
          changed_admins = true;
        };
      for (auto u: admin_r)
      {
        auto r = user_key(ifnt, u, mountpoint);
        check_group_mount(r.second);
        add_admin(r.first, r.second, true, false);
      }
      for (auto u: admin_rw)
      {
        auto r = user_key(ifnt, u, mountpoint);
        check_group_mount(r.second);
        add_admin(r.first, r.second, true, true);
      }
      for (auto u: admin_remove)
      {
        auto r = user_key(ifnt, u, mountpoint);
        check_group_mount(r.second);
        auto del = [&r](auto& cont)
          {
            cont.erase(std::remove(cont.begin(), cont.end(), r.first),
                       cont.end());
          };
        del(dht.admin_keys.r);
        del(dht.admin_keys.w);
        del(dht.admin_keys.group_r);
        del(dht.admin_keys.group_w);
        changed_admins = true;
      }
      if (!peer.empty())
        dht.peers = parse_peers(peer);
      auto desc = [&]
        {
          if (output_name)
          {
            auto output = cli.get_output(output_name);
            elle::serialization::json::serialize(network, *output, false);
            return std::make_unique<infinit::NetworkDescriptor>(std::move(network));
          }
          else
          {
            ifnt.network_save(owner, network, true);
            cli.report_updated("linked network", network.name);
            auto res
              = std::make_unique<infinit::NetworkDescriptor>(std::move(network));
            cli.report_updated("network", res->name);
            return res;
          }
        }();
      if (push || push_network)
        {
          ifnt.beyond_push("network", desc->name, *desc, owner, false, true);
          this->cli().report_action("pushed", "network", desc->name);
          // FIXME: report.
        }
      if (changed_admins && !output_name)
        std::cout << "INFO: Changes to network admins do not affect existing data:\n"
                  << "INFO: Admin access will be updated on the next write to each\n"
                  << "INFO: file or folder.\n";
    }
  }
}
