LDAP Integration
================

1. Overview
-----------

While Infinit retains its own authentication mechanism using an RSA key pair, it can use an LDAP server for:

- Authenticating users for the 'login' operation, i.e. the initial key pair import on a new device.
- Exporting LDAP groups to an Infinit network or drive.
- Inviting LDAP users to an Infinit network or drive.

2. Creating LDAP users on the Hub
---------------------------------

To create an Infinit user on the Hub using LDAP, use the `infinit-user` command with its `--ldap-name`, passing the full LDAP distinguished name (DN) as argument:

```
$> infinit-user --create --push --full --name jdoe --ldap-name cn=jdoe,ou=engineering,dc=mycompany,dc=com
```

Once completed, the user will be able to login to the GUI and/or fetch their user key pair from the Hub using 'jdoe' as login, and their LDAP password:

```
$> infinit-user --login --name jdoe --password MY-LDAP-PASSWORD
```

3. Batch import LDAP users to the Hub
-------------------------------------

If you would like to import many users, you can use the `infinit-ldap` binary with the `--populate-beyond` action. This will fetch all the LDAP users matching a query, and create a Hub user for each.

Here are a few examples of invocation, followed by a detailed explanation of the arguments:

```
$> infinit-ldap --populate-beyond --server ldap.company.com --domain dc=company,dc=com --user admin --searchbase '' --fullname-pattern '$(cn)' --username-pattern '$(uid)' --email-pattern '$(uid)@company.com'
$> infinit-ldap --populate-beyond --server ldap.company.com --domain dc=company,dc=com --user cn=bob,dc=company,dc=com --searchbase 'ou=engineering' --fullname-pattern '$(cn)' --username-pattern '$(uid)' --email-pattern '$(mail)'

```

### LDAP authentication arguments ###

- `--server`: URL of the LDAP server to connect to.
- `--domain`: LDAP domain.
- `--user`: LDAP username (admin, cn=admin).
- `--password`: LDAP password (will be prompted for if not given).


### LDAP query arguments ###

- `--searchbase`: Starting node for the query (defaults to the root).
- `--object-class`: If set, the query will match all LDAP entries with given objectClass (defaults to 'person').
- `--filter`: Raw LDAP query to use (defaults to 'objectClass=person').


### User creation parameters ###

For each LDAP record matching the query, one Infinit user will be created and saved to the Hub. This user needs to have a unique username, an email address, and a fullname, which are extracted from the LDAP record using patterns. A pattern is simply a string with embedded references to LDAP fields wrapped in `$()`

- `--username-pattern`: Pattern that defines the username.
- `--fullname-pattern`: Pattern that defines the fullname.
- `--email-pattern`: Pattern that defines the email address.

4. Batch invite users to an Infinit network or drive
----------------------------------------------------

The `infinit-ldap` binary with the `--populate-network` action can be used to:

- Invite LDAP users to an Infinit network or drive.
- Pre-register those users onto the network (so that `infinit-acl` can be used
to give them permissions immediately).
- Import LDAP groups into Infinit groups.

This command requires a running mounted volume on the target network to work. To import all groups from LDAP to Infinit, you would use a command structured as follows:

```
$> infinit-ldap --populate-network --server ldap.company.com --domain dc=company,dc=com -u admin --mountpoint Volumes/network -n network --searchbase '' --object-class posixGroup --as bob --drive bob/drive
```

What follows are descriptions of the various options for the `--populate-network` action.

### LDAP authentication arguments ###

- `--server`: URL of the LDAP server to connect to.
- `--domain`: LDAP domain.
- `--user`: LDAP username (admin, cn=admin).
- `--password`: LDAP password (will be prompted for if not given).

### LDAP query arguments ###

- `--searchbase`: Starting node for the query (defaults to the root).
- `--object-class`: If set, the query will match all LDAP entries with given objectClass (defaults to 'posixGroup').
- `--filter`: Raw LDAP query to use (defaults to 'objectClass=posixGroup').

### Network/volume arguments ###

- `--network`: Name of the network.
- `--as`: Infinit user to perform action as.
- `--drive`: Full name of the drive (optional, if unset registration will occur only at network level).
- `--deny-write`: Creates a read-only passport.
- `--deny-storage`: Creates a passport that cannot provide storage.
- `--mountpoint`: Path to a mounted volume on the target network.

The LDAP query can return a mixed set of user and group records. For each group, the group itself will be imported and named after the LDAP 'cn', and all group members will be registered.
