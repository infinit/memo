LDAP Integration
================

Infinit for Enterprise provides LDAP integration so that existing user and group structures can be used.

Overview
--------

Infinit currently provides the following LDAP functionality:

- Authentication of users using their LDAP credentials.
- Importing LDAP groups to for volume ACLs.
- Inviting LDAP users to networks and drives.

In order to provide this LDAP integration, Infinit relies on an [on premise Hub](xxx link). This allows users to benefit from Infinit's ease of use while keeping all data on your infrastructure.

Configuring the on premise Hub to use LDAP
------------------------------------------
xxx pending docker image of the hub

The Infinit LDAP binary
-----------------------

The `infinit-ldap` binary facilitates batch LDAP operations. These include batch importing users to the on premise Hub as well as replicating the LDAP structure in a volume's ACL and inviting LDAP users to drives.

### LDAP authentication arguments ###

In order to fetch LDAP data, Infinit needs to be given the server address, credentials and the domain to use.

- `--server`: URL of the LDAP server to connect to.
- `--domain`: LDAP domain.
- `--user`: LDAP username (of the form: admin, cn=admin,<domain>).
- `--password`: LDAP password (default: prompt).

### LDAP query arguments ###

When performing operations, various search filters can be used. The default values are specific to the operation being performed and can be seen in the action's help.

- `--searchbase`: Starting node for the query (defaults to the root).
- `--object-class`: If set, the query will match all LDAP entries with given objectClass.
- `--filter`: Raw LDAP query to use.

Importing LDAP users to the Hub
-------------------------------

### Importing a single user ###

A single LDAP user can be added to the Hub using the `infinit-user` binary. As Infinit uses its own mechanism for authentication – an RSA key pair for each user – one will be created and uploaded to the on premise Hub.

```
$> infinit-user --signup --name asmith --ldap-name "cn=asmith,ou=engineering,dc=company,dc=com" --email asmith@company.com --fullname "Alice Smith" --full
Generating RSA keypair.
Locally generated user "asmith".
Remotely saved user "asmith".
```

The user will then be able to perform a login on the Hub using their Infinit user name and LDAP password. This fetches and stores their key pair.

```
$> infinit-user --login --name asmith
Password: ******
Locally saved user "asmith".
$> infinit-user --list
asmith: public/private keys
```

### Batch importing users ###

The `infinit-ldap` binary can be used to batch import LDAP users to the on premise Hub. The following command will fetch all the users in the _company.com_ domain, generating an Infinit user name from their `uid`, using their email as defined by `mail`, using a fullname as defined by their `givenName` followed by their `sn`. Note that only LDAP users who do not already have an account are registered.

```
$> infinit-ldap --populate-hub --server ldap.company.com --domain "dc=company,dc=com" --searchbase "" --user "admin" --username-pattern '$(uid)' --email-pattern '$(mail)' --fullname-pattern '$(givenName) $(sn)'
LDAP Password: ******
Fetched LDAP user "cn=asmith,ou=engineering,dc=infinit,dc=nodomain".

Will register the following users:
ewilliams: Eve Williams (ewilliams@company.com) DN: cn=ewilliams,ou=engineering,dc=infinit,dc=nodomain
bjones: Bob Jones (bjones@company.com) DN: cn=bjones,ou=marketing,dc=infinit,dc=nodomain

Proceed? [Y/n] Y
Remotely saved user "ewilliams".
Remotely saved user "bjones".
```

The `--searchbase` argument can be used to specify only users within a certain group, for example the organizational unit _engineering_. In this case, the command would be changed as follows:

```
$> infinit-ldap --populate-hub --server ldap.company.com --domain "dc=company,dc=com" --searchbase "ou=engineering" --user "admin" --username-pattern '$(uid)' --email-pattern '$(mail)' --fullname-pattern '$(givenName) $(sn)'
LDAP Password: ******
Fetched LDAP user "cn=asmith,ou=engineering,dc=infinit,dc=nodomain".

Will register the following users:
ewilliams: Eve Williams (ewilliams@company.com) DN: cn=ewilliams,ou=engineering,dc=infinit,dc=nodomain

Proceed? [y/n] y
Remotely saved user "ewilliams".
```

Once the users have been imported to the Hub, they can login using their Infinit user name and LDAP password as shown in the single user case.

As the fields used on each LDAP server may be slightly different, formatting options are provided so that Infinit user names, fullnames and email addresses can be automatically generated. The default values can be found in the action help and some examples of usage can be seen above.

- `--username-pattern`: Pattern that defines the username.
- `--fullname-pattern`: Pattern that defines the fullname.
- `--email-pattern`: Pattern that defines the email address.

Importing LDAP structures to networks
-------------------------------------

The `infinit-ldap` binary can be used to batch import LDAP users and groups into a network so that they can be used for volume ACLs.

There are several prerequisites that need to be met before you can proceed:

- The LDAPusers need to be [registered on the on premise Hub](#importing-ldap-users-to-the-hub).
- [Created a network](documentation/reference#create-a-network) and pushed it to your on premise Hub.
- [Create a volume](documentation/reference#create-a-volume) on the network, pushed it to the on premise Hub and [mounted it](documentation/reference#mount-a-volume) locally.

There are several steps that the command will automate. It will perform an LDAP search based on the [query arguments given](#ldap-query-arugments), fetching the desired POSIX groups, their associated users along with any other users that meet the search requirements. The corresponding Infinit users will then be fetched from the on premise Hub so that [passports](documentation/reference#create-a-passport) can be created and pushed to the on premise Hub and so that the users can registered to the network along with the POSIX groups.

What follows is an example of how this command could be called:

```
$> infinit-ldap --populate-network --as asmith --server ldap.company.com --domain "dc=example,dc=com" --searchbase "" --user "admin" --network my-network --mountpoint /mnt/my-volume
LDAP Password: ******
Fetched LDAP user "cn=asmith,ou=engineering,dc=company,dc=com".
Fetched LDAP user "cn=bjones,ou=marketing,dc=company,dc=company".
Fetched LDAP user "cn=ewilliams,ou=engineering,dc=company,dc=company".
Remotely saved passport "asmith/n: ewilliams".
Registered user "ewilliams" to network.
Remotely saved passport "asmith/n: bjones".
Registered user "bjones" to network.
Remotely saved passport "asmith/n: asmith".
User "asmith" already registerd to network.
Added group "marketing" to network.
Added user "bjones" to group "marketing" on network.
Added group "engineering" to network.
Added user "asmith" to group "engineering" on network.
Added user "ewilliams" to group "engineering" on network.
Added group "all" to network.
Added user "asmith" to group "all" on network.
Added user "bjones" to group "all" on network.
Added user "ewilliams" to group "all" on network.
```

Now that the network has been populated with the users and groups, we can perform [ACL operations](documentation/reference#access-control-list) using the `infinit-acl`binary.

```
$> infinit-acl --set --path /mnt/my-volume --mode rw --user bjones
```

Inviting LDAP users to a drive
------------------------------

The `infinit-ldap` binary can be used to batch invite LDAP users to a drive. These users need to be [registered on the on premise Hub](#importing-ldap-users-to-the-hub) first. Part of this process includes [importing the LDAP users and groups into the network](#importing-ldap-structures-to-networks).

There are several prerequisites that need to be met before you can proceed:

- The LDAPusers need to be [registered on the on premise Hub](#importing-ldap-users-to-the-hub).
- [Created a network](documentation/reference#create-a-network) and pushed it to your on premise Hub.
- [Create a volume](documentation/reference#create-a-volume) on the network, pushed it to the on premise Hub and [mounted it](documentation/reference#mount-a-volume) locally.
- [Create a drive](documentation/reference#create-a-drive) with the network and volume and push it to the on premise Hub.

There are several steps that the command will automate. It will perform an LDAP search based on the [query arguments given](#ldap-query-arugments), fetching the desired POSIX groups, their associated users along with any other users that meet the search requirements. The corresponding Infinit users will then be fetched from the on premise Hub so that [passports](documentation/reference#create-a-passport) can be created and pushed to the on premise Hub and so that the users can registered to the network along with the POSIX groups. Once registered, the users will be given permissions to the root of the volume as specified by the `--root-permissions`. If the `--create-home` flag is passed, each user will have a home directory created for them at `home/<username>`. The users will then be invited to the drive via email.

```
$> infinit-ldap --drive-invite --as asmith --server ldap.company.com --domain "dc=company,dc=com" --searchbase "" --user "admin" --mountpoint /mnt/my-volume --network my-network --drive my-drive --root-permissions --create-home
LDAP password: ******
Fetched LDAP user "cn=asmith,ou=engineering,dc=company,dc=com".
Fetched LDAP user "cn=bjones,ou=marketing,dc=company,dc=com".
Fetched LDAP user "cn=ewilliams,ou=engineering,dc=company,dc=com".
Remotely saved passport "asmith/n: ewilliams".
Registered user "ewilliams" to network.
Remotely saved passport "asmith/n: bjones".
Registered user "bjones" to network.
Remotely saved passport "asmith/n: asmith".
User "asmith" already registerd to network.
Added group "marketing" to network.
Added user "bjones" to group "marketing" on network.
Added group "engineering" to network.
Added user "asmith" to group "engineering" on network.
Added user "ewilliams" to group "engineering" on network.
Added group "all" to network.
Added user "asmith" to group "all" on network.
Added user "bjones" to group "all" on network.
Added user "ewilliams" to group "all" on network.
Created home directory: "/Volumes/v/home/ewilliams".
Remotely saved invitation "asmith/d: ewilliams".
Created home directory: "/Volumes/v/home/bjones".
Remotely saved invitation "asmith/d: bjones".
Created home directory: "/Volumes/v/home/asmith".
Remotely saved invitation "asmith/d: asmith".
```
