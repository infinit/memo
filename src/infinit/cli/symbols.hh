#pragma once

#include <das/Symbol.hh>

#include <infinit/utility.hh>

#ifdef INFINIT_WINDOWS
# undef stat
#endif

namespace infinit
{
  namespace cli
  {
    // Symbol, short option char (or 0), help string, whether positional argument.
    DAS_CLI_SYMBOL(account, '\0', "cloud service account name", false);
    DAS_CLI_SYMBOL(add, '\0', "add users, administrators and groups to group (prefix: @<group>, ^<admin>", false);
    DAS_CLI_SYMBOL(add_admin, '\0', "add administrator to group", false);
    DAS_CLI_SYMBOL(add_group, '\0', "add group to group", false);
    DAS_CLI_SYMBOL(add_user, '\0', "add user to group", false);
    DAS_CLI_SYMBOL(admin_r, 0, "Set admin users that can read all data", false);
    DAS_CLI_SYMBOL(admin_remove, 0, "Remove given users from all admin lists (prefix: @<group>, requires mountpoint)", false);
    DAS_CLI_SYMBOL(admin_rw, 0, "Set admin users that can read and write all data", false);
    DAS_CLI_SYMBOL(advertise_host, 0, "advertise extra endpoint using given host", false);
    DAS_CLI_SYMBOL(allow_create_passport, 0, "allow user to create passports for network", false);
    DAS_CLI_SYMBOL(allow_root_creation, 0, "create the filesystem root if not found" , false);
    DAS_CLI_SYMBOL(as, 'a', "user to run commands as", false);
    DAS_CLI_SYMBOL(async, 0, "use asynchronous write operations" , false);
    DAS_CLI_SYMBOL(avatar, '\0', "path to an image to use as avatar", false);
    DAS_CLI_SYMBOL(aws, 0, "Amazon Web Services (or S3 compatible) credentials", false);
    DAS_CLI_SYMBOL(bucket, '\0', "bucket name", false);
    DAS_CLI_SYMBOL(cache, 0, "enable caching with default values", false);
    DAS_CLI_SYMBOL(cache_disk_size, 0, "size of disk cache for immutable data in bytes (default: 512 MB)", false);
    DAS_CLI_SYMBOL(cache_ram_invalidation, 0, "RAM block cache invalidation time in seconds (default: 15 seconds)", false);
    DAS_CLI_SYMBOL(cache_ram_size, 0, "maximum RAM block cache size in bytes (default: 64 MB)", false);
    DAS_CLI_SYMBOL(cache_ram_ttl, 0, "RAM block cache time-to-live in seconds (default: 5 minute)", false);
    DAS_CLI_SYMBOL(capacity, 'c', "limit storage capacity (use: B,kB,kiB,MB,MiB,GB,GiB,TB,TiB)", false);
    DAS_CLI_SYMBOL(clear_content, '\0', "remove all blocks from disk (filesystem storage only)", false);
    DAS_CLI_SYMBOL(compatibility_version, '\0', "compatibility version to force", false);
    DAS_CLI_SYMBOL(create_root, 'R', "create root directory", false);
    DAS_CLI_SYMBOL(daemon, 'd', "run as a background daemon" , false);
    DAS_CLI_SYMBOL(default_permissions, 'd', "default permissions (optional: r,rw)", false);
    DAS_CLI_SYMBOL(deny_storage, '\0', "deny user ability to contribute storage to the network", false);
    DAS_CLI_SYMBOL(deny_write, '\0', "deny user write access to the network", false);
    DAS_CLI_SYMBOL(description, '\0', "{type} description", false);
    DAS_CLI_SYMBOL(disable_UTF_8_conversion, 0, "disable FUSE conversion of UTF-8 to native format", false);
    DAS_CLI_SYMBOL(disable_inherit, '\0', "make new files and directories not inherit permissions", false);
    DAS_CLI_SYMBOL(dropbox, '\0', "store blocks on Dropbox", false);
    DAS_CLI_SYMBOL(email, 'e', "user email", false);
    DAS_CLI_SYMBOL(enable_inherit, 'i', "make new files and directories inherit permissions", false);
    DAS_CLI_SYMBOL(encrypt, 0,  "use encryption: no, lazy, yes (default: yes)", false);
    DAS_CLI_SYMBOL(endpoint, '\0', "S3 endpoint", false);
    DAS_CLI_SYMBOL(endpoints_file, 0, "write node listening endpoints to file (format: host:port)", false);
    DAS_CLI_SYMBOL(eviction_delay, 'e', "missing servers eviction delay\n(default: 10 min)", false);
    DAS_CLI_SYMBOL(fallback_xattrs, '\0', "use fallback special file if extended attributes are not supported", false);
    DAS_CLI_SYMBOL(fetch, 'f', "fetch {type} from {hub}", false);
    DAS_CLI_SYMBOL(fetch_drive, 0, "update local drive descriptor from {hub}", false);
    DAS_CLI_SYMBOL(fetch_endpoints, 0, "fetch endpoints from {hub}" , false);
    DAS_CLI_SYMBOL(fetch_endpoints_interval, 0, "period for repolling endpoints from the Hub in seconds", false);
    DAS_CLI_SYMBOL(filesystem, '\0', "store blocks on local filesystem (default)", false);
    DAS_CLI_SYMBOL(finder_sidebar, 0, "show volume in Finder sidebar" , false);
    DAS_CLI_SYMBOL(full, '\0', "include private key (do not use unless you understand the implications", false);
    DAS_CLI_SYMBOL(fullname, '\0', "user full name", false);
    DAS_CLI_SYMBOL(fuse_option, 0, "option to pass directly to FUSE" , false);
    DAS_CLI_SYMBOL(gcs, '\0', "store blocks on Google Cloud Storage", false);
    DAS_CLI_SYMBOL(google_drive, '\0', "store blocks on Google Drive", false);
    DAS_CLI_SYMBOL(group, 'g', "group {action} {type} for", false);
    DAS_CLI_SYMBOL(help, 'h', "show this help message", false);
    DAS_CLI_SYMBOL(home, 'h', "create a home directory for the invited user", false);
    DAS_CLI_SYMBOL(host, '\0', "SSH host", false);
    DAS_CLI_SYMBOL(icon, 'i', "path to an image to use as icon", false);
    DAS_CLI_SYMBOL(input, 'i', "file to read {type} from", false);
    DAS_CLI_SYMBOL(k, 0, "number of groups (default: 1)", false);
    DAS_CLI_SYMBOL(kalimero, 0, "use a Kalimero overlay network. Used for local testing", false);
    DAS_CLI_SYMBOL(kelips, 0, "use a Kelips overlay network (default)", false);
    DAS_CLI_SYMBOL(kelips_contact_timeout, 0, "ping timeout before considering a peer lost (default: 2min)", false);
    DAS_CLI_SYMBOL(key, 'k', "RSA key pair in PEM format - e.g. your SSH key", false);
    DAS_CLI_SYMBOL(kouncil, 0, "use a Kouncil overlay network", false);
    DAS_CLI_SYMBOL(ldap_name, 'l', "user LDAP distinguished name", false);
    DAS_CLI_SYMBOL(listen, 0, "specify which IP address to listen on (default: all)", false);
    DAS_CLI_SYMBOL(map_other_permissions, 0, "allow chmod to set world permissions", false);
    DAS_CLI_SYMBOL(mode, 'm', "access mode {action}: r, w, rw, none", false);
    DAS_CLI_SYMBOL(monitoring, 0, "enable monitoring", false);
    DAS_CLI_SYMBOL(mount_icon, 0, "path to an icon for mounted volume" , false);
    DAS_CLI_SYMBOL(mount_name, 0, "name of mounted volume" , false);
    DAS_CLI_SYMBOL(mountpoint, 'm', "where to mount the filesystem" , false);
    DAS_CLI_SYMBOL(name, 'n', "name of the {type} {action}", true);
    DAS_CLI_SYMBOL(network, 'N', "network {action} {type} for", false);
    DAS_CLI_SYMBOL(no_avatar, '\0', "do not {action} avatars", false);
    DAS_CLI_SYMBOL(no_consensus, 0, "use no consensus algorithm", false);
    DAS_CLI_SYMBOL(no_countdown, 0, "do not show countdown timer", false);
    DAS_CLI_SYMBOL(no_local_endpoints, 0, "Disable automatic detection of local endpoints", false);
    DAS_CLI_SYMBOL(no_public_endpoints, 0, "Disable automatic detection of public endpoints", false);
    DAS_CLI_SYMBOL(nodes, 0, "estimate of the total number of nodes", false);
    DAS_CLI_SYMBOL(operation, 'O', "operation to {action}", false);
    DAS_CLI_SYMBOL(others_mode, 'o', "access mode {action} for other users: r, w, rw, none", false);
    DAS_CLI_SYMBOL(output, 'o', "file to write the {type} to", false);
    DAS_CLI_SYMBOL(passphrase, 0, "passphrase to secure identity (default: prompt for passphrase)", false);
    DAS_CLI_SYMBOL(passport, 0, "create passports for each invitee", false);
    DAS_CLI_SYMBOL(password, 'P', "password to authenticate with {hub}", false);
    DAS_CLI_SYMBOL(path, '\0', "file whose {type} {action}", false);
    DAS_CLI_SYMBOL(paths, 'p', "paths to blocks", false);
    DAS_CLI_SYMBOL(paxos, 0, "use Paxos consensus algorithm (default)", false);
    DAS_CLI_SYMBOL(paxos_rebalancing_auto_expand, 0, "whether to automatically rebalance under-replicated blocks", false);
    DAS_CLI_SYMBOL(paxos_rebalancing_inspect, 0, "whether to inspect all blocks on startup and trigger rebalancing", false);
    DAS_CLI_SYMBOL(peer, 0, "peer address or file with list of peer addresses (host:port)" , false);
    DAS_CLI_SYMBOL(peers_file, 0, "Periodically write list of known peers to given file", false);
    DAS_CLI_SYMBOL(permissions, 0, "set default user permissions to XXX", false);
    DAS_CLI_SYMBOL(port, 0, "outbound port to use", false);
    DAS_CLI_SYMBOL(port_file, 0, "write node listening port to file", false);
    DAS_CLI_SYMBOL(protocol, 0, "RPC protocol to use: tcp, utp, all (default: all)", false);
    DAS_CLI_SYMBOL(publish, 0, "alias for --fetch-endpoints --push-endpoints" , false);
    DAS_CLI_SYMBOL(pull, '\0', "pull {type} from {hub}", false);
    DAS_CLI_SYMBOL(purge, '\0', "purge objects owned by the {type}", false);
    DAS_CLI_SYMBOL(push, 'p', "push {type} to {hub}", false);
    DAS_CLI_SYMBOL(push_drive, '\0', "push drive to {hub}", false);
    DAS_CLI_SYMBOL(push_endpoints, 0, "push endpoints to {hub}" , false);
    DAS_CLI_SYMBOL(push_invitations, '\0', "update remote drive descriptor and send invitations to {hub}", false);
    DAS_CLI_SYMBOL(push_network, 0, "push the network to {hub}", false);
    DAS_CLI_SYMBOL(push_passport, 0, "push passport to {hub}", false);
    DAS_CLI_SYMBOL(push_user, 0, "push user to {hub}", false);
    DAS_CLI_SYMBOL(push_volume, 0, "push the volume to {hub}" , false);
    DAS_CLI_SYMBOL(readonly, 0, "mount as readonly" , false);
    DAS_CLI_SYMBOL(receive, 0, "receive an object from another device using {hub}", false);
    DAS_CLI_SYMBOL(recursive, 'R', "{verb} {type} recursively", false);
    DAS_CLI_SYMBOL(region, '\0', "AWS region", false);
    DAS_CLI_SYMBOL(register_service, 'r', "register volume in the network", false);
    DAS_CLI_SYMBOL(remove, '\0', "remove users, administrators and groups from group (prefix: @<group>, ^<admin>", false);
    DAS_CLI_SYMBOL(remove_admin, '\0', "remove administrator from group", false);
    DAS_CLI_SYMBOL(remove_group, '\0', "remove group from group", false);
    DAS_CLI_SYMBOL(remove_user, '\0', "remove user from group", false);
    DAS_CLI_SYMBOL(replication_factor, 'r', "data replication factor (default: 1)", false);
    DAS_CLI_SYMBOL(s3, '\0', "store blocks on AWS S3", false);
    DAS_CLI_SYMBOL(script, 's', "suppress extraneous human friendly messages and use JSON output", false);
    DAS_CLI_SYMBOL(service, 0, "fetch {type} from the network, not beyond", false);
    DAS_CLI_SYMBOL(show, '\0', "list group users, administrators and description", false);
    DAS_CLI_SYMBOL(ssh, '\0', "store blocks via SSH", false);
    DAS_CLI_SYMBOL(stat, '\0', "show the remaining asynchronous operations count and size", false);
    DAS_CLI_SYMBOL(storage, 'S', "storage to contribute (optional, data striped over multiple)", false);
    DAS_CLI_SYMBOL(storage_class, '\0', "storage class to use: STANDARD, STANDARD_IA, REDUCED_REDUNDANCY (default: bucket default)", false);
    DAS_CLI_SYMBOL(traverse, 't', "set read permission on parent directories", false);
    DAS_CLI_SYMBOL(user, 'u', "user {action} {type} for", false);
    DAS_CLI_SYMBOL(value, 'v', "value {action}", false);
    DAS_CLI_SYMBOL(verbose, '\0', "use verbose output", false);
    DAS_CLI_SYMBOL(volume, 'V', "associated volume name", false);

    
    DAS_SYMBOL(acl);
    DAS_SYMBOL(block);
    DAS_SYMBOL(call);
    DAS_SYMBOL(create);
    DAS_SYMBOL(credentials);
    DAS_SYMBOL(describe);
    DAS_SYMBOL(deserialize);
    DAS_SYMBOL(device);
    DAS_SYMBOL(drive);
    DAS_SYMBOL(get_xattr);
    DAS_SYMBOL(hash);
    DAS_SYMBOL(import);
    DAS_SYMBOL(invite);
    DAS_SYMBOL(join);
    DAS_SYMBOL(journal);
    DAS_SYMBOL(link);
    DAS_SYMBOL(list);
    DAS_SYMBOL(list_services);
    DAS_SYMBOL(list_storage);
    DAS_SYMBOL(login);
    DAS_SYMBOL(mount);
    DAS_SYMBOL(run);
    DAS_SYMBOL(set);
    DAS_SYMBOL(set_xattr);
    DAS_SYMBOL(signup);
    DAS_SYMBOL(silo);
    DAS_SYMBOL(start);
    DAS_SYMBOL(status);
    DAS_SYMBOL(stats);
    DAS_SYMBOL(stop);
    DAS_SYMBOL(transmit);
    DAS_SYMBOL(unlink);
    DAS_SYMBOL(update);
    DAS_SYMBOL(version);

    DAS_SYMBOL_NAMED(delete, delete_);
    DAS_SYMBOL_NAMED(export, export_);
    DAS_SYMBOL_NAMED(register, register_);

    // Modes must be defined twice: once as a regular symbol, then as
    // a `modes::mode_*` symbol.
    namespace modes
    {
      DAS_SYMBOL(mode_add);
      DAS_SYMBOL(mode_create);
      DAS_SYMBOL(mode_delete);
      DAS_SYMBOL(mode_describe);
      DAS_SYMBOL(mode_deserialize);
      DAS_SYMBOL(mode_export);
      DAS_SYMBOL(mode_fetch);
      DAS_SYMBOL(mode_get_xattr);
      DAS_SYMBOL(mode_group);
      DAS_SYMBOL(mode_hash);
      DAS_SYMBOL(mode_import);
      DAS_SYMBOL(mode_invite);
      DAS_SYMBOL(mode_join);
      DAS_SYMBOL(mode_link);
      DAS_SYMBOL(mode_list);
      DAS_SYMBOL(mode_list_services);
      DAS_SYMBOL(mode_list_storage);
      DAS_SYMBOL(mode_login);
      DAS_SYMBOL(mode_mount);
      DAS_SYMBOL(mode_pull);
      DAS_SYMBOL(mode_push);
      DAS_SYMBOL(mode_receive);
      DAS_SYMBOL(mode_register);
      DAS_SYMBOL(mode_run);
      DAS_SYMBOL(mode_set);
      DAS_SYMBOL(mode_set_xattr);
      DAS_SYMBOL(mode_signup);
      DAS_SYMBOL(mode_start);
      DAS_SYMBOL(mode_stat);
      DAS_SYMBOL(mode_stats);
      DAS_SYMBOL(mode_status);
      DAS_SYMBOL(mode_stop);
      DAS_SYMBOL(mode_transmit);
      DAS_SYMBOL(mode_unlink);
      DAS_SYMBOL(mode_update);
    }
  }
}
