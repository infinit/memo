// http://opensource.apple.com/source/mDNSResponder/mDNSResponder-576.30.4/mDNSPosix/PosixDaemon.c
#if __APPLE__
# define daemon yes_we_know_that_daemon_is_deprecated_in_os_x_10_5_thankyou
#endif

#include <memory>

#include <elle/json/exceptions.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/serialization/json.hh>

#ifndef INFINIT_WINDOWS
# include <reactor/network/unix-domain-socket.hh>
#endif

#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/MonitoringServer.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Configuration.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/storage/Strip.hh>

ELLE_LOG_COMPONENT("infinit-network");

#include <main.hh>
#include <xattrs.hh>

infinit::Infinit ifnt;

#if __APPLE__
# undef daemon
extern "C" int daemon(int, int);
#endif

namespace dnut = infinit::model::doughnut;

namespace
{
  std::vector<infinit::model::Endpoints>
  parse_peers(std::vector<std::string> const& speers)
  {
    return elle::make_vector(speers, [&](auto const& s)
      {
        auto comps = std::vector<std::string>{};
        boost::algorithm::split(comps, s, boost::is_any_of(","));
        infinit::model::Endpoints eps;
        try
        {
          for (auto const& s: comps)
            eps.emplace_back(s);
        }
        catch (elle::Error const& e)
        {
          elle::err("Malformed endpoints '%s': %s", s, e);
        }
        return eps;
      });
  }

  std::unique_ptr<infinit::storage::StorageConfig>
  storage_configuration(boost::program_options::variables_map const& args)
  {
    auto res = std::unique_ptr<infinit::storage::StorageConfig>{};
    auto storage_count = args.count("storage");
    if (storage_count > 0)
    {
      auto storages = args["storage"].as<std::vector<std::string>>();
      auto backends
        = std::vector<std::unique_ptr<infinit::storage::StorageConfig>>{};
      for (auto const& s: storages)
        backends.emplace_back(ifnt.storage_get(s));
      if (backends.size() == 1)
        return std::move(backends[0]);
      else
        return std::make_unique<infinit::storage::StripStorageConfig>
          (std::move(backends));
    }
    return {};
  }
}

COMMAND(create)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  std::unique_ptr<infinit::overlay::Configuration> overlay_config;
  {
    int overlays =
      + (args.count("kalimero") ? 1 : 0)
      + (args.count("kelips") ? 1 : 0)
      + (args.count("kouncil") ? 1 : 0)
      ;
    if (overlays > 1)
      elle::err<CommandLineError>("only one overlay type must be specified");
  }
  if (args.count("kalimero"))
    overlay_config = std::make_unique<infinit::overlay::KalimeroConfiguration>();
  if (args.count("kouncil"))
    overlay_config = std::make_unique<infinit::overlay::kouncil::Configuration>();
  else // default to Kelips
  {
    auto kelips =
      std::make_unique<infinit::overlay::kelips::Configuration>();
    if (args.count("k"))
      kelips->k = args["k"].as<int>();
    else if (args.count("nodes"))
    {
      int nodes = args["nodes"].as<int>();
      if (nodes < 10)
        kelips->k = 1;
      else if (sqrt(nodes) < 5)
        kelips->k = nodes / 5;
      else
        kelips->k = sqrt(nodes);
    }
    else
      kelips->k = 1;
    if (auto timeout = optional<std::string>(args, "kelips-contact-timeout"))
      kelips->contact_timeout_ms =
        std::chrono::duration_from_string<std::chrono::milliseconds>(*timeout)
        .count();
    if (args.count("encrypt"))
    {
      std::string enc = args["encrypt"].as<std::string>();
      if (enc == "no")
      {
        kelips->encrypt = false;
        kelips->accept_plain = true;
      }
      else if (enc == "lazy")
      {
        kelips->encrypt = true;
        kelips->accept_plain = true;
      }
      else if (enc == "yes")
      {
        kelips->encrypt = true;
        kelips->accept_plain = false;
      }
      else
        elle::err<CommandLineError>("'encrypt' must be 'no', 'lazy' or 'yes': %s",
                                    enc);
    }
    else
    {
      kelips->encrypt = true;
      kelips->accept_plain = false;
    }
    if (args.count("protocol"))
      kelips->rpc_protocol = infinit::protocol_get(args);
    overlay_config = std::move(kelips);
  }
  auto storage = storage_configuration(args);
  // Consensus
  auto consensus_config =
    std::unique_ptr<dnut::consensus::Configuration>{};
  {
    int replication_factor = 1;
    if (args.count("replication-factor"))
      replication_factor = args["replication-factor"].as<int>();
    if (replication_factor < 1)
      elle::err<CommandLineError>("replication factor must be greater than 0");
    auto eviction = optional<std::string>(args, "eviction-delay");
    bool no_consensus = args.count("no-consensus");
    bool paxos = args.count("paxos");
    if (!no_consensus)
      paxos = true;
    if (1 < no_consensus + paxos)
      elle::err<CommandLineError>("more than one consensus specified");
    if (paxos)
    {
      consensus_config = std::make_unique<
        dnut::consensus::Paxos::Configuration>(
          replication_factor,
          eviction ?
          std::chrono::duration_from_string<std::chrono::seconds>(*eviction) :
          std::chrono::seconds(10 * 60));
    }
    else
    {
      if (replication_factor != 1)
        elle::err("without consensus, replication factor must be 1");
      consensus_config = std::make_unique<
        dnut::consensus::Configuration>();
    }
  }
  dnut::AdminKeys admin_keys;
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
  if (args.count("admin-r"))
  {
    auto admins = args["admin-r"].as<std::vector<std::string>>();
    for (auto const& a: admins)
      add_admin(ifnt.user_get(a).public_key, true, false);
  }
  if (args.count("admin-rw"))
  {
    auto admins = args["admin-rw"].as<std::vector<std::string>>();
    for (auto const& a: admins)
      add_admin(ifnt.user_get(a).public_key, true, true);
  }
  auto port = optional<int>(args, "port");
  auto peers = std::vector<infinit::model::Endpoints>{};
  if (args.count("peer"))
  {
    auto speers = args["peer"].as<std::vector<std::string>>();
    peers = parse_peers(speers);
  }
  auto dht =
    std::make_unique<dnut::Configuration>(
      infinit::model::Address::random(0), // FIXME
      std::move(consensus_config),
      std::move(overlay_config),
      std::move(storage),
      owner.keypair(),
      std::make_shared<infinit::cryptography::rsa::PublicKey>(owner.public_key),
      dnut::Passport(
        owner.public_key,
        ifnt.qualified_name(name, owner),
        infinit::cryptography::rsa::KeyPair(owner.public_key,
                                            owner.private_key.get())),
      owner.name,
      std::move(port),
      version,
      admin_keys,
      peers);
  {
    auto network = infinit::Network(ifnt.qualified_name(name, owner),
                                    std::move(dht),
                                    optional(args, "description"));
    auto desc = std::unique_ptr<infinit::NetworkDescriptor>{};
    if (args.count("output"))
    {
      auto output = get_output(args);
      elle::serialization::json::serialize(network, *output, false);
      desc.reset(new infinit::NetworkDescriptor(std::move(network)));
    }
    else
    {
      ifnt.network_save(owner, network);
      desc.reset(new infinit::NetworkDescriptor(std::move(network)));
      ifnt.network_save(*desc);
      report_created("network", desc->name);
    }
    if (option_push(args, {"push-network"}))
      beyond_push("network", desc->name, *desc, owner);
  }
}

namespace
{
  std::pair<infinit::cryptography::rsa::PublicKey, bool>
  user_key(std::string name, boost::optional<std::string> mountpoint)
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
    int res = port_getxattr(*mountpoint, "infinit.group.control_key." + name, buf, 16384, true);
    if (res <= 0)
      elle::err("Unable to fetch group %s", name);
    elle::Buffer b(buf, res);
    elle::IOStream is(b.istreambuf());
    auto key = elle::serialization::json::deserialize
      <infinit::cryptography::rsa::PublicKey>(is);
    return std::make_pair(key, is_group);
  }
}

COMMAND(update)
{
  auto name = mandatory(args, "name", "network name");
  auto description = optional(args, "description");
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(name, owner);
  if (description)
    network.description = description;
  network.ensure_allowed(owner, "update");
  auto& dht = *network.dht();
  if (auto port = optional<int>(args, "port"))
    dht.port = port.get();
  if (infinit::compatibility_version)
    dht.version = infinit::compatibility_version.get();
  bool changed_admins = false;
  auto check_group_mount = [&args, &network] (bool group)
    {
      if (group && !args.count("mountpoint"))
        elle::err<CommandLineError>("must specify mountpoint of volume on "
                                    "network \"%s\" to edit group admins",
                                    network.name);
    };
  auto add_admin = [&dht] (infinit::cryptography::rsa::PublicKey const& key,
                           bool group, bool read, bool write)
    {
      if (read && !write)
      {
        auto& target = group ? dht.admin_keys.group_r : dht.admin_keys.r;
        if (std::find(target.begin(), target.end(), key) == target.end())
          target.push_back(key);
      }
      if (write) // Implies RW.
      {
        auto& target = group ? dht.admin_keys.group_w : dht.admin_keys.w;
        if (std::find(target.begin(), target.end(), key) == target.end())
          target.push_back(key);
      }
    };
  if (args.count("admin-r"))
  {
    for (auto u: args["admin-r"].as<std::vector<std::string>>())
    {
      auto r = user_key(u, optional(args, "mountpoint"));
      check_group_mount(r.second);
      add_admin(r.first, r.second, true, false);
    }
    changed_admins = true;
  }
  if (args.count("admin-rw"))
  {
    for (auto u: args["admin-rw"].as<std::vector<std::string>>())
    {
      auto r = user_key(u, optional(args, "mountpoint"));
      check_group_mount(r.second);
      add_admin(r.first, r.second, true, true);
    }
    changed_admins = true;
  }
  if (args.count("admin-remove"))
  {
    for (auto u: args["admin-remove"].as<std::vector<std::string>>())
    {
      auto r = user_key(u, optional(args, "mountpoint"));
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
    }
    changed_admins = true;
  }
  if (args.count("peer"))
  {
    auto speers = args["peer"].as<std::vector<std::string>>();
    auto peers = parse_peers(speers);
    dht.peers = peers;
  }
  auto desc = [&]
    {
      if (args.count("output"))
      {
        auto output = get_output(args);
        elle::serialization::json::serialize(network, *output, false);
        return std::make_unique<infinit::NetworkDescriptor>(std::move(network));
      }
      else
      {
        ifnt.network_save(owner, network, true);
        report_updated("linked network", network.name);
        auto res = std::make_unique<infinit::NetworkDescriptor>(std::move(network));
        report_updated("network", res->name);
        return res;
      }
    }();
  if (option_push(args, {"push-network"}))
    beyond_push("network", desc->name, *desc, owner, true, false, true);
  if (changed_admins && !args.count("output"))
    std::cout << "INFO: Changes to network admins do not affect existing data:\n"
              << "INFO: Admin access will be updated on the next write to each\n"
              << "INFO: file or folder.\n";
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto output = get_output(args);
  auto network_name = mandatory(args, "name", "network name");
  auto desc = ifnt.network_descriptor_get(network_name, owner);
  network_name = desc.name;
  elle::serialization::json::serialize(desc, *output, false);
  report_exported(*output, "network", network_name);
}

COMMAND(fetch)
{
  auto owner = self_user(ifnt, args);
  auto network_name_ = optional(args, "name");
  auto save = [&owner] (infinit::NetworkDescriptor desc_) {
    // Save or update network descriptor.
    ifnt.network_save(desc_, true);
    for (auto const& u: ifnt.network_linked_users(desc_.name))
    {
      // Copy network descriptor.
      auto desc = desc_;
      auto network = ifnt.network_get(desc.name, u, false);
      if (network.model)
      {
        auto* d = dynamic_cast<dnut::Configuration*>(
          network.model.get()
        );
        infinit::Network updated_network(
          desc.name,
          std::make_unique<dnut::Configuration>(
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
  if (network_name_)
  {
    auto network_name = ifnt.qualified_name(network_name_.get(), owner);
    save(infinit::beyond_fetch<infinit::NetworkDescriptor>("network", network_name));
  }
  else // Fetch all networks for owner.
  {
    auto res =
      infinit::beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::NetworkDescriptor>>>(
        elle::sprintf("users/%s/networks", owner.name),
        "networks for user",
        owner.name,
        owner);
    for (auto const& n: res["networks"])
      save(n);
  }
}

COMMAND(import)
{
  auto input = get_input(args);
  auto desc =
    elle::serialization::json::deserialize<infinit::NetworkDescriptor>
    (*input, false);
  ifnt.network_save(desc);
  report_imported("network", desc.name);
}

COMMAND(link_)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  {
    auto network = ifnt.network_get(network_name, owner, false);
    if (network.model)
      elle::err("%s is already linked with %s", network.name, owner.name);
  }
  auto storage = storage_configuration(args);
  auto desc = ifnt.network_descriptor_get(network_name, owner);
  auto passport = [&] () -> infinit::Passport
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
  bool ok = passport.verify(
    passport.certifier() ? *passport.certifier() : desc.owner);
  if (!ok)
    elle::err("passport signature is invalid");
  if (storage && !passport.allow_storage())
    elle::err("passport does not allow storage");
  auto network = infinit::Network(
    desc.name,
    std::make_unique<dnut::Configuration>(
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
  auto has_output = optional(args, "output");
  auto output = has_output ? get_output(args) : nullptr;
  if (output)
    infinit::save(*output, network, false);
  else
  {
    ifnt.network_save(owner, network, true);
    report_action("linked", "device to network", network.name);
  }
}

COMMAND(list)
{
  auto owner = self_user(ifnt, args);
  if (script_mode)
  {
    elle::json::Array l;
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

COMMAND(unlink_)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name, owner, true);
  ifnt.network_unlink(network.name, owner, true);
}

COMMAND(push)
{
  auto network_name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(network_name, owner);
  {
    auto& dht = *network.dht();
    auto owner_uid = infinit::User::uid(*dht.owner);
    infinit::NetworkDescriptor desc(std::move(network));
    beyond_push("network", desc.name, desc, owner, true, false, true);
  }
}

COMMAND(pull)
{
  auto name_ = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network_name = ifnt.qualified_name(name_, owner);
  beyond_delete("network", network_name, owner, false, flag(args, "purge"));
}


COMMAND(delete_)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(name, owner, false);
  bool purge = flag(args, "purge");
  bool unlink = flag(args, "unlink");
  bool pull = flag(args, "pull");
  auto linked_users = ifnt.network_linked_users(network.name);
  if (linked_users.size() && !unlink)
  {
    auto user_names = elle::make_vector(linked_users,
                                        [](auto const& u) { return u.name; });
    elle::err("Network is still linked with this device by %s. "
              "Please unlink it first or add the --unlink flag",
              user_names);
  }
  if (purge)
  {
    auto volumes = ifnt.volumes_for_network(network.name);
    auto drives = std::vector<std::string>{};
    for (auto const& volume: volumes)
    {
      auto vol_drives = ifnt.drives_for_volume(volume);
      drives.insert(drives.end(), vol_drives.begin(), vol_drives.end());
    }
    for (auto const& drive: drives)
    {
      auto drive_path = ifnt._drive_path(drive);
      if (boost::filesystem::remove(drive_path))
        report_action("deleted", "drive", drive, "locally");
    }
    for (auto const& volume: volumes)
    {
      auto vol_path = ifnt._volume_path(volume);
      auto v = ifnt.volume_get(volume);
      boost::filesystem::remove_all(v.root_block_cache_dir());
      if (boost::filesystem::remove(vol_path))
        report_action("deleted", "volume", volume, "locally");
    }
    for (auto const& user: ifnt.user_passports_for_network(network.name))
    {
      auto passport_path = ifnt._passport_path(network.name, user);
      if (boost::filesystem::remove(passport_path))
      {
        report_action("deleted", "passport",
                      elle::sprintf("%s: %s", network.name, user),
                      "locally");
      }
    }
  }
  if (pull)
    beyond_delete("network", network.name, owner, true, purge);
  ifnt.network_delete(name, owner, unlink, true);
}

namespace
{
  using Action =
    std::function<void (infinit::User& owner,
                        infinit::Network& network,
                        dnut::Doughnut& dht,
                        bool push,
                        bool script_mode)>;
  void
  network_run(boost::program_options::variables_map const& args,
              Action const& action)
  {
    auto name = mandatory(args, "name", "network name");
    auto owner = self_user(ifnt, args);
    auto network = ifnt.network_get(name, owner);
    {
      auto rebalancing_auto_expand = optional<bool>(
        args, "paxos-rebalancing-auto-expand");
      auto rebalancing_inspect = optional<bool>(
        args, "paxos-rebalancing-inspect");
      if (rebalancing_auto_expand || rebalancing_inspect)
      {
        auto paxos = dynamic_cast<
          dnut::consensus::Paxos::Configuration*>(
            network.dht()->consensus.get());
        if (!paxos)
          throw CommandLineError("paxos options on non-paxos consensus");
        if (rebalancing_auto_expand)
          paxos->rebalance_auto_expand(rebalancing_auto_expand.get());
        if (rebalancing_inspect)
          paxos->rebalance_inspect(rebalancing_inspect.get());
      }
    }
    network.ensure_allowed(owner, "run");
    bool cache = flag(args, option_cache);
    auto cache_ram_size = optional<int>(args, option_cache_ram_size);
    auto cache_ram_ttl = optional<int>(args, option_cache_ram_ttl);
    auto cache_ram_invalidation =
      optional<int>(args, option_cache_ram_invalidation);
    auto disk_cache_size = optional<uint64_t>(args, option_cache_disk_size);
    cache |= (cache_ram_size || cache_ram_ttl || cache_ram_invalidation
              || disk_cache_size);
    auto port = optional<int>(args, option_port);
    auto listen_address_str = optional<std::string>(args, option_listen_interface);
    auto listen_address
      = listen_address_str
      ? boost::asio::ip::address::from_string(*listen_address_str)
      : boost::optional<boost::asio::ip::address>{};
    auto dht = network.run(
      owner,
      false,
      cache, cache_ram_size, cache_ram_ttl, cache_ram_invalidation,
      flag(args, "async"), disk_cache_size, infinit::compatibility_version, port,
      listen_address,
      flag(args, "monitoring"));
    hook_stats_signals(*dht);
    if (auto plf = optional(args, "peers-file"))
    {
      auto more_peers = infinit::hook_peer_discovery(*dht, *plf);
      ELLE_TRACE("Peer list file got %s peers", more_peers.size());
      if (!more_peers.empty())
        dht->overlay()->discover(more_peers);
    }
    if (args.count("peer"))
    {
      auto peers = args["peer"].as<std::vector<std::string>>();
      auto eps
        = elle::make_vector(peers,
                            [](auto const& peer)
                            {
                              if (boost::filesystem::exists(peer))
                                return infinit::endpoints_from_file(peer);
                              else
                                return infinit::model::Endpoints({peer});
                            });
      dht->overlay()->discover(eps);
    }
    // Only push if we have are contributing storage.
    bool push = option_push(args, {"push-endpoints"}) &&
      dht->local() && dht->local()->storage();
    bool fetch = option_fetch(args, {"fetch-endpoints"});
    if (!dht->local() && (!script_mode || push))
      elle::err("network %s is client only since no storage is attached", name);
    if (dht->local())
    {
      if (auto port_file = optional(args, option_port_file))
        infinit::port_to_file(dht->local()->server_endpoint().port(), port_file.get());
      if (auto endpoint_file = optional(args, option_endpoint_file))
        infinit::endpoints_to_file(
          dht->local()->server_endpoints(), endpoint_file.get());
    }
#ifndef INFINIT_WINDOWS
    infinit::DaemonHandle daemon_handle;
    if (flag(args, "daemon"))
      daemon_handle = infinit::daemon_hold(0, 1);
#endif
    auto poll_beyond = optional<int>(args, option_poll_beyond);
    auto run = [&, push]
      {
        reactor::Thread::unique_ptr poll_thread;
        if (fetch)
        {
          infinit::model::NodeLocations eps;
          network.beyond_fetch_endpoints(eps);
          dht->overlay()->discover(eps);
          if (poll_beyond && *poll_beyond > 0)
            poll_thread =
              network.make_poll_beyond_thread(*dht, eps, *poll_beyond);
        }
#ifndef INFINIT_WINDOWS
        if (flag(args, "daemon"))
        {
          ELLE_TRACE("releasing daemon");
          infinit::daemon_release(daemon_handle);
        }
#endif
        action(owner, network, *dht, push, script_mode);
      };
    if (push)
    {
      auto advertise = optional<std::vector<std::string>>(args, "advertise-host");
      elle::With<InterfacePublisher>(
        network, owner, dht->id(),
        dht->local()->server_endpoint().port(),
        advertise,
        flag(args, "no-local-endpoints"),
        flag(args, "no-public-endpoints")) << [&]
      {
        run();
      };
    }
    else
      run();
  }
}

COMMAND(run)
{
  network_run(
    args,
    [&] (infinit::User& owner,
         infinit::Network& network,
         dnut::Doughnut& dht,
         bool push,
         bool script_mode)
    {
      reactor::Thread::unique_ptr stat_thread;
      if (push)
        stat_thread = network.make_stat_update_thread(owner, dht);
      report_action("running", "network", network.name);
      if (script_mode)
      {
        auto input = infinit::commands_input(args);
        while (true)
        {
          try
          {
            auto json = boost::any_cast<elle::json::Object>(
              elle::json::read(*input));
            elle::serialization::json::SerializerIn command(json, false);
            command.set_context<dnut::Doughnut*>(&dht);
            auto op = command.deserialize<std::string>("operation");
            if (op == "fetch")
            {
              auto address =
                command.deserialize<infinit::model::Address>("address");
              auto block = dht.fetch(address);
              ELLE_ASSERT(block);
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
              response.serialize("value", block);
            }
            else if (op == "insert" || op == "update")
            {
              auto block = command.deserialize<
                std::unique_ptr<infinit::model::blocks::Block>>("value");
              if (!block)
                elle::err("missing field: value");
              dht.store(
                std::move(block),
                op == "insert" ?
                infinit::model::STORE_INSERT : infinit::model::STORE_UPDATE);
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
            }
            else if (op == "write_immutable")
            {
              auto block = dht.make_block<infinit::model::blocks::ImmutableBlock>(
                elle::Buffer(command.deserialize<std::string>("data")));
              auto addr = block->address();
              dht.store(std::move(block), infinit::model::STORE_INSERT);
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
              response.serialize("address", addr);
            }
            else if (op == "read")
            {
              auto block = dht.fetch(
                  command.deserialize<infinit::model::Address>("address"));
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
              response.serialize("data", block->data().string());
              int version = -1;
              if (auto mb = dynamic_cast<infinit::model::blocks::MutableBlock*>(block.get()))
                version = mb->version();
              response.serialize("version", version);
            }
            else if (op == "update_mutable")
            {
              auto addr = command.deserialize<infinit::model::Address>("address");
              auto version = command.deserialize<int>("version");
              auto data = command.deserialize<std::string>("data");
              auto block = dht.fetch(addr);
              auto& mb = dynamic_cast<infinit::model::blocks::MutableBlock&>(*block);
              if (mb.version() >= version)
                elle::err("Current version is %s", mb.version());
              mb.data(elle::Buffer(data));
              dht.store(std::move(block), infinit::model::STORE_UPDATE);
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
            }
            else if (op == "resolve_named")
            {
              auto name = command.deserialize<std::string>("name");
              bool create = command.deserialize<bool>("create_if_missing");
              auto addr = dnut::NB::address(*dht.owner(),
                name, dht.version());
              auto res = [&] {
                try
                {
                  auto block = dht.fetch(addr);
                  auto& nb = dynamic_cast<dnut::NB&>(*block);
                  return infinit::model::Address::from_string(nb.data().string());
                }
                catch (infinit::model::MissingBlock const& mb)
                {
                  if (!create)
                    elle::err("NB %s does not exist", name);
                  auto ab = dht.make_block<infinit::model::blocks::ACLBlock>();
                  auto addr = ab->address();
                  dht.store(std::move(ab), infinit::model::STORE_INSERT);
                  auto nb = dnut::NB(dht, name, elle::sprintf("%s", addr));
                  dht.store(nb, infinit::model::STORE_INSERT);
                  return addr;
                }
              }();
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
              response.serialize("address", res);
            }
            else if (op == "remove")
            {
              auto addr = command.deserialize<infinit::model::Address>("address");
              dht.remove(addr);
              auto response = elle::serialization::json::SerializerOut(
                std::cout, false, true);
              response.serialize("success", true);
            }
            else
              elle::err("invalid operation: %s", op);
          }
          catch (elle::Error const& e)
          {
            if (input->eof())
              return;
            auto response =elle::serialization::json::SerializerOut(
              std::cout, false, true);
            response.serialize("success", false);
            response.serialize("message", e.what());
          }
        }
      }
      else
        reactor::sleep();
    });
}

COMMAND(list_services)
{
  network_run(
    args,
    [&] (infinit::User& owner,
         infinit::Network& network,
         dnut::Doughnut& dht,
         bool /*push*/,
         bool script_mode)
    {
      auto services = dht.services();
      auto output = get_output(args);
      if (script_mode)
      {
        auto res = std::unordered_map<std::string, std::vector<std::string>>{};
        for (auto const& type: services)
        {
          auto services = elle::make_vector(type.second,
                                            [](auto const& service)
                                            {
                                              return service.first;
                                            });
          res.emplace(type.first, std::move(services));
        }
        elle::serialization::json::serialize(res, *output, false);
      }
      else
      {
        for (auto const& type: services)
        {
          *output << type.first << ":" << std::endl;
          for (auto const& service: type.second)
            *output << "  " << service.first << std::endl;
        }
      }
    });
}

COMMAND(list_storage)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name, owner);
  if (network.model->storage)
  {
    if (auto strip = dynamic_cast<infinit::storage::StripStorageConfig*>(
        network.model->storage.get()))
      for (auto const& s: strip->storage)
        std::cout << s->name << "\n";
    else
      std::cout << network.model->storage->name;
    std::cout << std::endl;
  }
}

COMMAND(stats)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network_name");
  auto name = ifnt.qualified_name(network_name, owner);
  auto res =
    infinit::beyond_fetch<Storages>(
      elle::sprintf("networks/%s/stat", name),
      "stat",
      "stat",
      boost::none,
      infinit::Headers(),
      false);

  // FIXME: write Storages::operator(std::ostream&)
  elle::printf("{\"usage\": %s, \"capacity\": %s}",
               res.usage, res.capacity);
}

#ifndef INFINIT_WINDOWS
COMMAND(inspect)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network_name");
  auto network = ifnt.network_get(network_name, owner);
  auto s_path = network.monitoring_socket_path(owner);
  if (!boost::filesystem::exists(s_path))
    elle::err("network not running or monitoring disabled");
  reactor::network::UnixDomainSocket socket(s_path);
  using Monitoring = infinit::model::MonitoringServer;
  using Query = infinit::model::MonitoringServer::MonitorQuery::Query;
  auto do_query = [&] (Query query_val)
    {
      auto query = Monitoring::MonitorQuery(query_val);
      elle::serialization::json::serialize(query, socket, false, false);
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(socket));
      return Monitoring::MonitorResponse(std::move(json));
    };
  auto print_response = [&] (Monitoring::MonitorResponse const& response)
    {
      if (script_mode)
        elle::json::write(*get_output(args), response.as_object());
      else
      {
        if (response.error)
          std::cout << "Error: " << response.error.get() << std::endl;
        if (response.result)
          std::cout << elle::json::pretty_print(response.result.get())
                    << std::endl;
        else
          std::cout << "Running" << std::endl;
      }
    };

  if (flag(args, "status"))
    print_response(do_query(Query::Status));
  else if (flag(args, "all"))
    print_response(do_query(Query::Stats));
  else if (flag(args, "redundancy"))
  {
    auto res = do_query(Query::Stats);
    if (res.result)
    {
      auto redundancy =
        boost::any_cast<elle::json::Object>(res.result.get()["redundancy"]);
      if (script_mode)
        elle::json::write(*get_output(args), redundancy);
      else
        std::cout << elle::json::pretty_print(redundancy) << std::endl;
    }
  }
  else if (flag(args, "peers"))
  {
    auto res = do_query(Query::Stats);
    if (res.result)
    {
      auto peer_list =
        boost::any_cast<elle::json::Array>(res.result.get()["peers"]);
      if (script_mode)
        elle::json::write(*get_output(args), peer_list);
      else
      {
        if (peer_list.size() == 0)
          std::cout << "No peers" << std::endl;
        else
          for (auto obj: peer_list)
          {
            auto json = boost::any_cast<elle::json::Object>(obj);
            std::cout << boost::any_cast<std::string>(json["id"]);
            json.erase("id");
            if (json.size())
              std::cout << ": " << elle::json::pretty_print(json) << std::endl;
          }
      }
    }
  }
  else
    elle::err<CommandLineError>("specify either \"--status\", \"--peers\","
                                " \"--redundancy\", or \"--all\"");
}
#endif

namespace
{
  std::vector<Mode::OptionDescription>
  run_options(std::vector<Mode::OptionDescription> opts = {})
  {
    using boost::program_options::value;
    using boost::program_options::bool_switch;
    opts.emplace_back("name,n", value<std::string>(), "network to run" );
    opts.emplace_back(
      "peer", value<std::vector<std::string>>()->multitoken(),
      "peer address or file with list of peer addresses (host:port)");
    opts.emplace_back("async", bool_switch(), "use asynchronous operations");
    opts.emplace_back(option_cache);
    opts.emplace_back(option_cache_ram_size);
    opts.emplace_back(option_cache_ram_ttl);
    opts.emplace_back(option_cache_ram_invalidation);
    opts.emplace_back(option_cache_disk_size);
    opts.emplace_back("fetch-endpoints", bool_switch(),
                      elle::sprintf("fetch endpoints from %s", infinit::beyond(true)));
    opts.emplace_back("fetch,f", bool_switch(), "alias for --fetch-endpoints");
    opts.emplace_back("push-endpoints", bool_switch(),
                      elle::sprintf("push endpoints to %s", infinit::beyond(true)));
    opts.emplace_back("push,p", bool_switch(), "alias for --push-endpoints");
    opts.emplace_back("publish", bool_switch(),
                      "alias for --fetch-endpoints --push-endpoints");
    opts.emplace_back(option_endpoint_file);
    opts.emplace_back(option_port_file);
    opts.emplace_back(option_port);
    opts.emplace_back("peers-file", value<std::string>(),
                      "file to write peers to periodically");
    opts.emplace_back(option_listen_interface);
    opts.emplace_back(option_poll_beyond);
    opts.emplace_back(option_no_local_endpoints);
    opts.emplace_back(option_no_public_endpoints);
    opts.emplace_back(option_advertise_host);
    return opts;
  }
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionsDescription overlay_types_options("Overlay types");
  overlay_types_options.add_options()
    ("kelips", "use a Kelips overlay network (default)")
    ("kalimero", "use a Kalimero overlay network.\nUsed for local testing")
    ("kouncil", "use a Kouncil overlay network")
    ;
  Mode::OptionsDescription consensus_types_options("Consensus types");
  consensus_types_options.add_options()
    ("paxos", "use Paxos consensus algorithm (default)")
    ("no-consensus", "use no consensus algorithm")
    ;
  Mode::OptionsDescription kelips_options("Kelips options");
  kelips_options.add_options()
    ("nodes", value<int>(), "estimate of the total number of nodes")
    ("k", value<int>(), "number of groups (default: 1)")
    ("kelips-contact-timeout", value<std::string>(),
     "ping timeout before considering a peer lost (default: 2 min)")
    ("encrypt", value<std::string>(),
      "use encryption: no,lazy,yes (default: yes)")
    ("protocol", value<std::string>(),
      "RPC protocol to use: tcp,utp,all\n(default: all)")
    ;
  Modes modes {
    {
      "create",
      "Create a network",
      &create,
      "--name NAME "
        "[OVERLAY-TYPE OVERLAY-OPTIONS...] "
        "[CONSENSUS-TYPE CONSENSUS-OPTIONS...] "
        "[--storage STORAGE...]",
      {
        { "name,n", value<std::string>(), "created network name" },
        option_description("network"),
        { "storage,S", value<std::vector<std::string>>()->multitoken(),
          "storage to contribute (optional, data striped over multiple)" },
        { "port", value<int>(), "port to listen on (default: random)" },
        { "replication-factor,r", value<int>(),
          "data replication factor (default: 1)" },
        { "eviction-delay,e", value<std::string>(),
          "missing servers eviction delay\n(default: 10 min)" },
        option_output("network"),
        { "push-network", bool_switch(),
          elle::sprintf("push the network to %s", infinit::beyond(true)) },
        { "push,p", bool_switch(), "alias for --push-network" },
        { "admin-r", value<std::vector<std::string>>()->multitoken(),
          "Set admin users that can read all data" },
        { "admin-rw", value<std::vector<std::string>>()->multitoken(),
          "Set admin users that can read and write all data" },
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "List of known node endpoints"}
      },
      {
        consensus_types_options,
        overlay_types_options,
        kelips_options,
      },
    },
    {
      "update",
      "Update a network",
      &update,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "network to update" },
        option_description("network"),
        { "port", value<int>(), "port to listen on (default: random)" },
        option_output("network"),
        { "push-network", bool_switch(),
          elle::sprintf("push the updated network to %s", infinit::beyond(true)) },
        { "push,p", bool_switch(), "alias for --push-network" },
        { "admin-r", value<std::vector<std::string>>()->multitoken(),
          "Add an admin user that can read all data\n"
          "(prefix: @<group>, requires mountpoint)" },
        { "admin-rw", value<std::vector<std::string>>()->multitoken(),
          "Add an admin user that can read and write all data\n"
          "(prefix: @<group>, requires mountpoint)" },
        { "admin-remove", value<std::vector<std::string>>()->multitoken(),
          "Remove given users from all admin lists\n"
          "(prefix: @<group>, requires mountpoint)" },
        { "mountpoint,m", value<std::string>(),
          "Mountpoint of a volume using this network, "
          "required to add admin groups" },
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "List of known node endpoints"}
      },
      {},
    },
    {
      "export",
      "Export a network",
      &export_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to export" },
        option_output("network"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a network from %s", infinit::beyond(true)),
      &fetch,
      {},
      {
        { "name,n", value<std::string>(), "network to fetch (optional)" },
      },
    },
    {
      "import",
      "Import a network",
      &import,
      {},
      {
        option_input("network"),
      },
    },
    {
      "link",
      "Link this device to a network",
      &link_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to link to" },
      },
      {},
      // Hidden options.
      {
        { "storage,S", value<std::vector<std::string>>()->multitoken(),
          "storage to contribute (optional)" },
        option_output("network"),
      },
    },
    {
      "unlink",
      "Unlink this device from a network",
      &unlink_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to unlink from" },
      },
      {},
      // Hidden options.
      {
        option_output("network"),
      },
    },
    {
      "list",
      "List networks",
      &list,
      {},
    },
    {
      "push",
      elle::sprintf("Push a network to %s", infinit::beyond(true)),
      &push,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to push" },
      },
    },
    {
      "delete",
      "Delete a network locally",
      &delete_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to delete" },
        { "pull", bool_switch(),
          elle::sprintf("pull the network if it is on %s", infinit::beyond(true)) },
        { "purge", bool_switch(), "remove objects that depend on the network" },
        { "unlink", bool_switch(), "automatically unlink network if linked" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a network from %s", infinit::beyond(true)),
      &pull,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to remove" },
        { "purge", bool_switch(), "remove objects that depend on the network" },
      },
    },
    {
      "run",
      "Run a network",
      &run,
      "--name NETWORK",
      run_options({
        option_input("commands"),
#ifndef INFINIT_WINDOWS
        { "daemon,d", bool_switch(), "run as a background daemon"},
        option_monitoring,
#endif
        }),
      {},
        // Hidden options
      {
        { "paxos-rebalancing-auto-expand", value<bool>(),
            "whether to automatically rebalance under-replicated blocks"},
        { "paxos-rebalancing-inspect", value<bool>(),
            "whether to inspect all blocks on startup and trigger rebalancing"},
      }
    },
    {
      "list-services",
      "List network registered services",
      &list_services,
      "--name NETWORK",
      run_options(),
      {},
    },
    {
      "list-storage",
      "List all storage contributed by this device to a network",
      &list_storage,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
      },
    },
    {
      "stats",
      elle::sprintf("Fetch stats of a network on %s", infinit::beyond(true)),
      &stats,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
      },
    },
#ifndef INFINIT_WINDOWS
    {
      "inspect",
      "Get information about a running network",
      &inspect,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
        { "status", bool_switch(), "check if network is running" },
        { "peers", bool_switch(), "list connected peers" },
        { "all", bool_switch(), "all informtation" },
        { "redundancy", bool_switch(), "describe data redundancy" },
      }
    },
#endif
  };
  return infinit::main("Infinit network management utility", modes, argc, argv);
}
