#pragma once

#include <das/Symbol.hh>

#include <infinit/utility.hh>

namespace infinit
{
  namespace cli
  {
    DAS_CLI_SYMBOL(as, 'a', "user to run commands as", false);
    DAS_CLI_SYMBOL(avatar, '\0', "path to an image to use as avatar", false);
    DAS_SYMBOL(call);
    DAS_CLI_SYMBOL(compatibility_version, '\0', "compatibility version to "
                   "force", false);
    DAS_SYMBOL(create);
    DAS_SYMBOL_NAMED(delete, delete_);
    DAS_CLI_SYMBOL(description, '\0', "{type} description", false);
    DAS_SYMBOL_NAMED(export, export_);
    DAS_CLI_SYMBOL(email, 'e', "user email", false);
    DAS_SYMBOL(fetch);
    DAS_CLI_SYMBOL(full, '\0', "include private key (do not use unless you "
                   "understand the implications", false);
    DAS_CLI_SYMBOL(fullname, '\0', "user full name", false);
    DAS_SYMBOL(hash);
    DAS_CLI_SYMBOL(help, 'h', "show this help message", false);
    DAS_SYMBOL(import);
    DAS_CLI_SYMBOL(input, 'i', "file to read {type} from", false);
    DAS_CLI_SYMBOL(key, 'k', "RSA key pair in PEM format - e.g. your SSH key",
                   false);
    DAS_CLI_SYMBOL(ldap_name, 'l', "user LDAP distinguished name", false);
    DAS_SYMBOL(list);
    DAS_SYMBOL(login);
    DAS_CLI_SYMBOL(name, 'n', "name of the {type} {action}", true);
    DAS_CLI_SYMBOL(no_avatar, '\0', "do not {action} avatars", false);
    DAS_CLI_SYMBOL(output, 'o', "file to write the {type} to", false);
    DAS_CLI_SYMBOL(password, 'P', "password to authenticate with {hub}",
                   false);
    DAS_CLI_SYMBOL(pull, '\0', "pull {type} from {hub}", false);
    DAS_CLI_SYMBOL(purge, '\0', "purge objects owned by the user", false);
    DAS_CLI_SYMBOL(push, 'p', "push {type} to {hub}", false);
    DAS_CLI_SYMBOL(push_user, '\0', "push user to {hub}", false);
    DAS_SYMBOL(signup);
    DAS_CLI_SYMBOL(script, 's', "suppress extraneous human friendly messages "
                   "and use JSON output", false);
    DAS_SYMBOL(silo);
    DAS_SYMBOL(user);
    DAS_SYMBOL(version);
    namespace modes
    {
      DAS_SYMBOL(mode_create);
      DAS_SYMBOL(mode_delete);
      DAS_SYMBOL(mode_export);
      DAS_SYMBOL(mode_fetch);
      DAS_SYMBOL(mode_hash);
      DAS_SYMBOL(mode_import);
      DAS_SYMBOL(mode_list);
      DAS_SYMBOL(mode_login);
      DAS_SYMBOL(mode_push);
      DAS_SYMBOL(mode_pull);
      DAS_SYMBOL(mode_signup);
    }
  }
}
