#pragma once

#include <das/Symbol.hh>

#include <infinit/utility.hh>

namespace infinit
{
  namespace cli
  {
    // Symbol, short option char (or 0), help string, whether positional argument.
    DAS_CLI_SYMBOL(account, '\0', "cloud service account name", false);
    DAS_CLI_SYMBOL(as, 'a', "user to run commands as", false);
    DAS_CLI_SYMBOL(avatar, '\0', "path to an image to use as avatar", false);
    DAS_CLI_SYMBOL(aws, 0, "Amazon Web Services (or S3 compatible) credentials", false);
    DAS_CLI_SYMBOL(bucket, '\0', "bucket name", false);
    DAS_CLI_SYMBOL(capacity, 'c', "limit storage capacity (use: B,kB,kiB,MB,MiB,GB,GiB,TB,TiB)", false);
    DAS_CLI_SYMBOL(clear_content, '\0', "remove all blocks from disk (filesystem storage only)", false);
    DAS_CLI_SYMBOL(compatibility_version, '\0', "compatibility version to force", false);
    DAS_CLI_SYMBOL(description, '\0', "{type} description", false);
    DAS_CLI_SYMBOL(dropbox, '\0', "store blocks on Dropbox", false);
    DAS_CLI_SYMBOL(email, 'e', "user email", false);
    DAS_CLI_SYMBOL(endpoint, '\0', "S3 endpoint", false);
    DAS_CLI_SYMBOL(fetch, 'f', "fetch {type} from {hub}", false);
    DAS_CLI_SYMBOL(fetch_drive, 0, "update local drive descriptor from {hub}", false);
    DAS_CLI_SYMBOL(filesystem, '\0', "store blocks on local filesystem (default)", false);
    DAS_CLI_SYMBOL(full, '\0', "include private key (do not use unless you understand the implications", false);
    DAS_CLI_SYMBOL(fullname, '\0', "user full name", false);
    DAS_CLI_SYMBOL(gcs, '\0', "store blocks on Google Cloud Storage", false);
    DAS_CLI_SYMBOL(google_drive, '\0', "store blocks on Google Drive", false);
    DAS_CLI_SYMBOL(help, 'h', "show this help message", false);
    DAS_CLI_SYMBOL(home, 'h', "create a home directory for the invited user", false);
    DAS_CLI_SYMBOL(host, '\0', "SSH host", false);
    DAS_CLI_SYMBOL(icon, 'i', "path to an image to use as icon", false);
    DAS_CLI_SYMBOL(input, 'i', "file to read {type} from", false);
    DAS_CLI_SYMBOL(key, 'k', "RSA key pair in PEM format - e.g. your SSH key", false);
    DAS_CLI_SYMBOL(ldap_name, 'l', "user LDAP distinguished name", false);
    DAS_CLI_SYMBOL(name, 'n', "name of the {type} {action}", true);
    DAS_CLI_SYMBOL(network, 'N', "associated network name", false);
    DAS_CLI_SYMBOL(no_avatar, '\0', "do not {action} avatars", false);
    DAS_CLI_SYMBOL(no_countdown, 0, "do not show countdown timer", false);
    DAS_CLI_SYMBOL(output, 'o', "file to write the {type} to", false);
    DAS_CLI_SYMBOL(passphrase, 0, "passphrase to secure identity (default: prompt for passphrase)", false);
    DAS_CLI_SYMBOL(passport, 0, "create passports for each invitee", false);
    DAS_CLI_SYMBOL(password, 'P', "password to authenticate with {hub}", false);
    DAS_CLI_SYMBOL(path, '\0', "directory where to store blocks", false);
    DAS_CLI_SYMBOL(paths, 'p', "paths to blocks", false);
    DAS_CLI_SYMBOL(permissions, 0, "set default user permissions to XXX", false);
    DAS_CLI_SYMBOL(pull, '\0', "pull {type} from {hub}", false);
    DAS_CLI_SYMBOL(purge, '\0', "purge objects owned by the {type}", false);
    DAS_CLI_SYMBOL(push, 'p', "push {type} to {hub}", false);
    DAS_CLI_SYMBOL(push_drive, '\0', "push drive to {hub}", false);
    DAS_CLI_SYMBOL(push_invitations, '\0', "update remote drive descriptor and send invitations to {hub}", false);
    DAS_CLI_SYMBOL(push_user, '\0', "push user to {hub}", false);
    DAS_CLI_SYMBOL(receive, 0, "receive an object from another device using {hub}", false);
    DAS_CLI_SYMBOL(region, '\0', "AWS region", false);
    DAS_CLI_SYMBOL(s3, '\0', "store blocks on AWS S3", false);
    DAS_CLI_SYMBOL(script, 's', "suppress extraneous human friendly messages and use JSON output", false);
    DAS_CLI_SYMBOL(ssh, '\0', "store blocks via SSH", false);
    DAS_CLI_SYMBOL(storage_class, '\0', "storage class to use: STANDARD, STANDARD_IA, REDUCED_REDUNDANCY (default: bucket default)", false);
    DAS_CLI_SYMBOL(user, 'u', "{action} user identity to another device using {hub}", false);
    DAS_CLI_SYMBOL(volume, 'V', "associated volume name", false);

    DAS_SYMBOL(add);
    DAS_SYMBOL(block);
    DAS_SYMBOL(call);
    DAS_SYMBOL(create);
    DAS_SYMBOL(credentials);
    DAS_SYMBOL(deserialize);
    DAS_SYMBOL(device);
    DAS_SYMBOL(drive);
    DAS_SYMBOL(hash);
    DAS_SYMBOL(import);
    DAS_SYMBOL(invite);
    DAS_SYMBOL(join);
    DAS_SYMBOL(list);
    DAS_SYMBOL(login);
    DAS_SYMBOL(signup);
    DAS_SYMBOL(silo);
    DAS_SYMBOL(transmit);
    DAS_SYMBOL(version);

    DAS_SYMBOL_NAMED(delete, delete_);
    DAS_SYMBOL_NAMED(export, export_);

    // Modes must be defined twice: once as a regular symbol, then as
    // a `modes::mode_*` symbol.
    namespace modes
    {
      DAS_SYMBOL(mode_add);
      DAS_SYMBOL(mode_create);
      DAS_SYMBOL(mode_delete);
      DAS_SYMBOL(mode_deserialize);
      DAS_SYMBOL(mode_export);
      DAS_SYMBOL(mode_fetch);
      DAS_SYMBOL(mode_hash);
      DAS_SYMBOL(mode_import);
      DAS_SYMBOL(mode_invite);
      DAS_SYMBOL(mode_join);
      DAS_SYMBOL(mode_list);
      DAS_SYMBOL(mode_login);
      DAS_SYMBOL(mode_pull);
      DAS_SYMBOL(mode_push);
      DAS_SYMBOL(mode_receive);
      DAS_SYMBOL(mode_signup);
      DAS_SYMBOL(mode_transmit);
    }
  }
}
