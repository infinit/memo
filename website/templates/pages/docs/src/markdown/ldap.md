LDAP Integration
================

1. Overview
-----------

While infinit retains its own authentication mechanism using RSA keypair, it can
use an LDAP server for:

- Authenticating users for the 'login' operation, i.e. the initial keypair
import on a new device.
- Exporting LDAP groups to an infinit network or drive.
- Inviting LDAP users to an infinit network or drive.


2. Creating LDAP users on the hub
---------------------------------

To create one infinit user on the hub with LDAP backend, use the `infinit-user`
command with its `--ldap-name`, passing the full LDAP distinguised name (DN) as
argument:

```
$> infinit-user --create --push --full --name jdoe --ldap-name cn=jdoe,ou=engineering,dc=mycompany,dc=com
```

Once done, the user will be able to login to the GUI and/or fetch its user key
from the hub using 'jdoe' as login, and his LDAP password:

```
$> infinit-user --login --name jdoe --password MY-LDAP-PASSWORD
```


3. Batch export LDAP users to the hub
-------------------------------------

If you need to import many users, you can use the `infinit-ldap --populate-beyond`
command. It will fetch all the LDAP users matching a query, and create a hub user
for each.

Here are a few exemples of invocation, followed by a detailed explaination of the
arguments:

```
$> infinit-ldap  --populate-beyond --server ldap.company.com --domain dc=company,dc=com --user admin --serachbase '' --fullname-pattern '$(cn)' --username-pattern '$(uid)' --email-pattern '$(uid)@company.com'
$> infinit-ldap  --populate-beyond --server ldap.company.com --domain dc=company,dc=com --user cn=bob,dc=company,dc=com --searchbase 'ou=engineering' --fullname-pattern '$(cn)' --username-pattern '$(uid)' --email-pattern '$(mail)'

```

### LDAP authentication arguments

- --server: URL of the LDAP server to connect to
- --domain: LDAP domain
- --user: LDAP username (admin, cn=admin)
- --password: LDAP password (will be prompted for if not given)


### LDAP query arguments

- --searchbase: Starting node for the query (defaults to the root).
- --object-class: If set, the query will match all LDAP entries with given objectClass.
Defaults to 'person'
- --filter: Raw LDAP query to use (defaults to 'objectClass=person').


### User creation parameters

For each LDAP record matching the query, one infinit user will be created and
exported to the hub. This user needs to have one unique username, an email
address, and a fullname, which are extracted from the LDAP record using
patterns.
A pattern is simply a string with embeded references to LDAP fields wrapped in
'$()'
- --username-pattern: Pattern that defines the username.
- --fullname-pattern: Pattern that defines the fullname.
- --email-pattern: Pattern that defines the email address.


4. Batch invite users to an infinit network or drive
----------------------------------------------------

The `infinit-ldap --populate-network` can be used to:
- Invite LDAP users to an infinit network or drive.
- Pre-register those users onto the network (so that `infinit-acl` can be used
to give them permissions immediately)
- Import LDAP groups into infinit groups.

This command requires a running mounted volume on the target network to work.
```
$> infinit-ldap --populate-network --server ldap.company.com --domain dc=company,dc=com -u admin --mountpoint Volumes/network -n network --searchbase '' --object-class posixGroup --as bob --drive bob/drive
```

### LDAP authentication arguments

- --server: URL of the LDAP server to connect to
- --domain: LDAP domain
- --user: LDAP username (admin, cn=admin)
- --password: LDAP password (will be prompted for if not given)


### LDAP query arguments

- --searchbase: Starting node for the query (defaults to the root).
- --object-class: If set, the query will match all LDAP entries with given objectClass.
Defaults to 'posixGroup'
- --filter: Raw LDAP query to use (defaults to 'objectClass=posixGroup').

### Network/volume arguments

- --network: name of the network
- --as: User to impersonate
- --drive: Full name of the drive (optional, if unset registration will occur only at network level
- --deny-write: Creates a read-only passport
- --deny-storage: Creates a passport that cannot provide storage
- --mountpoint: Path to a mounted volume on the target network

The ldap query can return a mixed set of user and group records.
For each group, the group itself will be imported and named after the ldap 'cn',
and all group members will be registered.
