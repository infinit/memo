Reference
========

This document goes through the command-line tools, describing how to perform specific tasks such as creating a storage network, contributing storage capacity, inviting users to join a drive and more.

Introduction
----------------

### Terminology ###

The Infinit command-line tools are composed of several binaries, each dealing with a specific resource/object.

A *user* represents the entity performing operations on files, directly or indirectly. Every user possesses an RSA key pair that is used to identify him/her. A user can create a *network* which represents the interconnection of computing resources that will compose the storage infrastructure. A *storage* is a storage resource, local or remote, that can be connected to a device to support part of the overall storage load. Finally, several *volumes* --- i.e. logical drives ---  can be created within a network.

The *Hub* is a cloud service whose role is to ease the process of discovery, sharing and more.

### Home ###

All the configuration files Infinit creates and uses are located in the `$INFINIT_DATA_HOME` directory which, by default, is set to `$HOME/.local/share/infinit/filesystem/`.

One can very easily set the environment variable to point to the directory of his/her choice, either by modifying the shell configuration or by setting it manually:

```
$> export INFINIT_DATA_HOME="/some/where/"
```

### Nomenclature ###

All the command-line tools rely on the same terminology when it comes to options and arguments. As an example, most binaries have options named `--create`, `--list`, `--fetch`, etc.

For more information on the options provided by a binary, do not hesitate to rely on the `--help` option. Note that the help is relative to the options already provided.

For example, the help for _infinit-user_ displays the general options available: export, create, import, list, etc.:

```
$> infinit-user --help
Usage: infinit-user MODE [OPTIONS...]

Infinit user utility:

Modes:
  --create                    Create a user
  --export                    Export a user so that it may be imported
                              elsewhere
  --fetch                     Fetch a user from the Hub
  --import                    Import a user
  --pull                      Remove a user from the Hub
  --delete                    Delete a user locally
  --push                      Push a user to the Hub
  --signup                    Create and push a user to the Hub
  --login                     Log the user to the Hub
  --list                      List users

Miscellaneous:
  -h [ --help ]               display the help
  -s [ --script ]             silence all extraneous human friendly messages
  -f [ --force-version ] arg  force used version
  -v [ --version ]            display version
  -a [ --as ] arg             user to run commands as (default: system user)
```

While the help when invoking the `--create` shows the options associated with creating a user:

```
$> infinit-user --create --help
Usage: infinit-user --create [OPTIONS...]

Create a user:

Create options:
  -n [ --name ] arg      user name (default: system user)
  -k [ --key ] arg       RSA key pair in PEM format - e.g. your SSH key
                         (default: generate key pair)
  --push-user            push the user to the Hub
  -p [ --push ]          alias for --push-user
  --email arg            valid email address (mandatory when using --push-user)
  --fullname arg         user's fullname (optional)
  --full                 include private key in order to facilitate device
                         pairing and fetching lost keys
  --password arg         password to authenticate with the Hub. Used with
                         --full (default: prompt for password)
```

Every binary follows the same semantic with the first option representing the mode of operation (a verb): `--create`, `--pull`, `--list`, `--delete`, `--export`, etc.

The name/identifier of the object on which you wish to operate can be specified through the `--name` option or simply by providing it outside of any option. As such both commands below are equivalent:

```
$> infinit-volume --push --name personal
$> infinit-volume --push personal
```

With the exception of the _infinit-user_ binary, one can specify the Infinit user behind the action by relying on the `--as` option followed by the user name. If not specified, the `$INFINIT_USER` environment variable is used, unless not set in which case the system user name is used.

### Hub ###

All objects (users, storages, networks, volumes etc.) are created locally by default with no server involved. The creation process may generate one or more files and store them in the `$INFINIT_DATA_HOME` directory.

The command-line tools however provide a way to rely on the Hub for certain operations in order to simplify some administrative tasks such as inviting a user to a drive, sharing the volumes created within a network, moving the user identity to another of your devices and so on. In addition, some functionalities such as the consumption of storage capacity in a network are only available through the Hub. As a rule of thumb, we advise you to always rely on the Hub, unless you know exactly what you are doing.

The use of the Hub can be activated through specific options, mostly `--push`, `--fetch` and `--pull`. The `--push` option publishes an object on the Hub for other users to retrieve it. The `--pull` option does the exact opposite, removing the object from the Hub. Finally, the `--fetch` option retrieves a resource from the Hub, e.g. a network descriptor, and stores it locally in the `$INFINIT_DATA_HOME` directory.

One can decide to either create objects locally before pushing them to the Hub or to perform both tasks through a single action by specifying `--push` option when invoking the command.

Note that some binaries operate in hub mode by default. For instance the _infinit-drive_ binary would not make sense without the Hub since its role is to bridge the gap between a low-level storage infrastructure and potential non-tech-savvy users.

**IMPORTANT**: This document mainly covers flows involving the Hub. For users wanting to use Infinit in a pure decentralized environment, the `--push`/`--fetch` operations must be replaced with `--export`/`--import` operations and the resulting files must be manually shared with other users and moved between devices. You will sometimes need to provide additional information such as the IP address of bootstrap nodes in order to discover the other nodes of a network.

User
------

The _infinit-user_ binary allows one to create a user identity, publish it to the Hub so that it can be referenced by other users and perform other user based operations.

### Create a user ###

A user is not much more than an RSA key pair that will be used to sign and encrypt data. The following creates a user, automatically generating a new RSA key pair.

Note that the name of the user is deduced from the system if you do not specify a name through the `--name` option or the `$INFINIT_USER` environment variable.

```
$> echo $USER
alice
$> infinit-user --create
Generating RSA keypair.
Locally generated user "alice".
```

You may want to specify the RSA key pair to use rather than generating a new one. You can use the `--key` option to reference a PEM-formatted RSA private key (e.g your SSH RSA key) and Infinit will create a user identity based on it.

```
$> infinit-user --create --name alice --key ~/.ssh/id_rsa
Key passphrase: ********
Locally generated user "alice".
```

_**WARNING**: The user identity file is the equivalent of an SSH key file and must therefore be kept private at all times._

### Sign up on the Hub ###

To register on the Hub, you can either use the `--push` option when creating your user, push the user once it has been created locally or sign up directly on the Hub.

To push an existing user, simply invoke _infinit-user_ with the `--push` mode and `--email` option to specify a valid email address. Needless to say that this email address will *never* be shared with third parties and will solely be used for Infinit to communicate news of its file storage platform.

**IMPORTANT**: Given the critical nature of the user identity, we strongly advise you to read the <a href="#log-in-on-another-device">Log in on another device</a> section in order to completely understand the ramifications of the options used when pushing your user.

```
$> infinit-user --push --name alice --fullname "Alice" --email alice@company.com
Remotely pushed user "alice".
```

Unfortunately, since names are unique, your user name may already be taken on the Hub, in which case the operation will fail. The action `--signup` has been introduced to overcome this problem, performing the equivalent of `--create --push` atomically, making sure that the user is created locally and remotely at once.

We advise users to sign up to the Hub before performing other operations to avoid complications:

```
$> infinit-user --signup --name alice --fullname "Alice" --email alice@company.com
Generating RSA keypair.
Remotely pushed user "alice".
```

### Fetch a user ###

One can very easily fetch the public identity of another user through the `--fetch` mode:

```
$> infinit-user --fetch --name bob
Fetched user "bob".
```

### List users ###

The list of users kept locally can contain both user identities that you created and therefore own as well as public identities of users that you fetched from the Hub for instance.

```
$> infinit-user --list
alice: public/private keys
bob: public key only
```

Credentials
---------------

The _infinit-credentials_ binary manages the credentials for your cloud services. Cloud services, such as Amazon S3 and Dropbox, can be used to add storage to your networks. Infinit considers these cloud services as basic and unprivileged datastores that are used to store blocks of encrypted data.

*__NOTE__: Because this binary requires the Hub for some types of credentials (such as Dropbox and Google), you may need to register your user on the Infinit Hub. For more information, please refer to the <a href="#user">User</a> section, more specifically how to <a href="#sign-up-on-the-hub">Sign up on the Hub</a>.*

### Add credentials ###

To add AWS credentials so that an Amazon S3 bucket can be used to store data, simply use the `--add` option specifying `aws`. Note that an Access Key ID and Secrect Access Key are used, not the user name and password:

```
$> infinit-credentials --add --aws --account s3-user
Please enter your AWS credentials
Access Key ID: AKIAIOSFODNN7EXAMPLE
Secret Access Key: ****************************************
Locally stored AWS credentials "s3-user".
```

_**IMPORTANT**: AWS credentials are only ever stored locally and cannot be pushed to the Hub. Never use the AWS root user. Always create a specific user, giving the user the minimum required permissions._

### List credentials ###

At any point, you can list your local credentials using the `--list` option:

```
$> infinit-credentials --list
AWS:
  AKIAIOSFODNN7EXAMPLE: s3-user
```

Storage
-----------

The _infinit-storage_ binary allows for the definition of storage resources. Such storage resources can be local — storing blocks of data on a locally available file system — or remote in which case the blocks of data are stored through a cloud service API.

Note that storage resources are device-specific. As such, resources cannot be pushed to the Hub since they only live locally.

### Create a storage resource ###

#### Locally ####

To create a storage resource on top of a local file system, simply specify the `--filesystem` option. You specify the path where the encrypted data blocks are stored using the `--path` option:

```
$> infinit-storage --create --filesystem --capacity 2GB --name local
Created storage "local".
```

#### Remotely ####

You can create a storage on top of a cloud service API. In order to do this, you will first need to add the cloud service's credentials using _infinit-credentials_, as shown in the <a href="#add-credentials">Add credentials</a> section.

You can then specify the type of cloud service you want your storage to rely upon along with the cloud service account identifier. Cloud service identifiers can be retrieved when <a href="#list-credentials">listing your credentials</a>.

In order to use Amazon S3, you must first have created an AWS user and an S3 bucket. Ensure that the user has permissions to read and write in the bucket.

The following creates a storage resource which uses a folder of an Amazon S3 bucket, specifying a name for the storage, the AWS account identifier, the region the bucket is in, the bucket's name and the folder to store the blocks in:

```
$> infinit-storage --create --s3 --name s3 --aws-account s3-user --region eu-central-1 --bucket my-s3-bucket --bucket-folder blocks-folder
Created storage "s3".
```

The list of supported cloud services is continually evolving and can be seen by using `--create --help`. Enterprise storage solutions such as <a href="https://cloud.google.com/storage">Google Cloud Storage</a> and <a href="https://www.backblaze.com/b2">Backblaze B2</a> as well as consumer oriented solutions such as <a href="https://www.dropbox.com">Dropbox</a> and <a href="https://www.google.com/drive">Google Drive</a> will be supported. If you would like any others, [let us know](http://infinit-sh.uservoice.com).

Network
-----------

With the _infinit-network_ utility you are able to create overlay networks, configure the way the distributed hash table behaves and much more.

### Create a network ###

The example below creates a network named _'cluster'_ which aggregates the storage resources controlled by the users involved in this network.

The network can be configured depending on the requirements of the storage infrastructure the administrator is setting up. For instance, the number of computing devices could be extremely small, the owners of those computers could be somewhat untrustworthy or their machines could be expected to be turned on and off throughout the day. To cater for this the network parameters can be tuned: the overlay's topology, the replication factor, the fault tolerance algorithm, etc.

The following creates a small storage network, relying on the Kelips overlay network with a replication factor of 3. In addition, the administrator decides to contribute two storage resources to the network on creation.

```
$> infinit-network --create --as alice --kelips --k 1 --replication-factor 3 --storage local --storage s3 --name cluster
Locally created network "alice/cluster".
```

The following overlay types are currently available:

- Kalimero: Simple test overlay supporting only one node.
- Stonehenge: Overlay supporting multiple storage nodes in a static configuration: the
list of peers must never change or be reordered once set.
- Kelips: Overlay with support for node churn. The _k_ argument specifies the
number of groups to use, each group being responsible for _1/kth_ of the files.
See the reference paper [_"Kelips: Building an Efficient and Stable P2P DHT through Increased Memory and Background Overhead"_](http://link.springer.com/chapter/10.1007/978-3-540-45172-3_15) for more information.

### Publish a network ###

You can now publish a network for other users to retrieve it. Note that the easiest way is always to append the `--push` option to the network creation command to perform both the creation and publication actions at once.

As with the other utilities, you can otherwise push the network to the Hub with the `--push` option:

```
$> infinit-network --push --as alice --name cluster
Remotely pushed network "alice/cluster".
```

You can also manipulate networks without relying on the Hub. Please refer to the `--export` and `--import` options in this case.

### List the networks ###

You can list the networks that you have locally using the `--list` mode. Do not forget that you may need to fetch the networks from the Hub in order to be up to date:

```
$> infinit-network --fetch --as alice
Fetched networks for user "alice".
$> infinit-network --list
alice/cluster
```

### Link a device to a network ###

Let us say that you want to connect a device to a network, this device being different from the one on which the network has been created but which is still used by the same user.

There are two ways to do this depending on who you are in relation to the network: its owner or an invited user.

#### As the owner ####

As the owner of the network, the system automatically recognizes you and allows you to link any of your devices to the network. The process in this case is straightforward.

When linking a device to a network, you can decide to contribute storage from the new device. In the example below, Alice connects one of her other devices and contributes storage capacity from her personal Network-Attached Storage (NAS).

```
$> infinit-network --link --as alice --name cluster --storage nas
Linked device to network "alice/cluster".
```

_**NOTE**: Keep in mind that the action of linking a device to a network must only be performed once on every new device._

#### As an invitee ####

In this case, you should have <a href="#receive-a-passport">received</a>, through the Hub or manually, an invitation to join a network created by someone else. Such invitations are called _passports_ and allow users to link their devices to the network they've been allowed to join.

In order to link a device to a network, you must first retrieve its descriptor. This network descriptor can be fetched from the Hub through a single command:

```
$> infinit-network --fetch --as bob --name alice/cluster
Fetched network "alice/cluster".
```

You now have both the network descriptor and a passport locally allowing you to link new devices to it. Let's link Bob's current device to Alice's 'cluster' network. Note that one can decide to contribute additional storage capacity through the `--storage` option.

```
$> infinit-network --link --as bob --name alice/cluster
Linked device to network "alice/cluster".
```

_**NOTE**: This process must be performed on each new device, proving that the user has indeed been allowed to join the network (via the passport) and that this device belongs to the user._

Passport
------------

The _infinit-passport_ binary is used to allow other users to join one's networks, granting him/her the right to link devices, contribute storage resources and potentially access files.

### Create a passport ###

To allow another user to join a network and link devices, you must issue him/her a passport. In order to reference the user to invite, you first need to obtain his/her user public identity.

Let's say that you want to invite the user 'bob' to your network. First you need to fetch his identity from the Hub (or retrieve it manually if operating without the Hub, see `--export` and `--import`):

```
$> infinit-user --fetch --as alice --name bob
Fetched user "bob".
```

The passport that you are about to create will be sealed, allowing only Bob to <a href="#link-a-device-to-a-network">connect new devices</a> to the network:

```
$> infinit-passport --create --as alice --network cluster --user bob
Locally created passport "alice/cluster: bob".
```

Now that the passport has been created, read the <a href="#distribute-a-passport">Distribute a passport</a> section to learn how to distribute it to the invited user.

### List the passports ###

You can list both the passports you've created for other users to join your networks and the passports issued by other users for you to join their networks:

```
$> infinit-passport --list --as alice
alice/cluster: bob
```

### Distribute a passport ###

Once the passport has been locally created, you must distribute it to the invited user for him/her to be able to join your network.

The easiest way to do this is to rely on the Hub, by appending the `--push` option when creating the passport; see the <a href="#create-a-passport">Create a passport</a> section.

You can otherwise push a local passport by invoking the `--push` action option as shown below:

```
$> infinit-passport --push --as alice --network cluster --user bob
Remotely pushed passport "alice/cluster: bob".
```

If you are using the pure decentralized environment i.e. without the Hub, you will need to manually export the passport and transmit it to the invited user in which case you should refer to the `--export` and `--import` options.

**IMPORTANT**: Be aware that the invited user will not be notified that there is a new passport for him/her to join your network. The invited user could detect this by fetching his/her passports and noticing a new one but that's about it. In order to speed things up, you should probably inform him/her through the medium of your choice: chat, email, carrier pigeon or else.

### Receive a passport ###

You can fetch your passports from the Hub in order to refresh the local snapshots using the `--fetch` option:

```
$> infinit-passport --fetch --as bob
Fetched passports for user "bob"
$> infinit-passport --list --as bob
alice/cluster: bob
```

*__NOTE__: The _infinit-passport_ binary also provides options to fetch all the passports for a specific user or for a specific network.*

That's it, you will now be able to <a href="#link-a-device-to-a-network">link devices to the networks</a> these passports allow you to.

Volume
----------

On top of the distributed storage layer i.e. the network, one can create a volume also known as logical drive. A volume is represented by the address of its root directory. While centralized file systems store this address in a specific block known as the _superblock_, Infinit uses a file located in the `$INFINIT_DATA_HOME` directory which describes the volume.

Note that several volumes can be created on the same network, which is analogous to partitions on the same hard disk drive.

### Create a volume ###

The command below creates a volume on a network. You can specify the default mount point for the volume through the `--mountpoint` option even though the following example does not:

```
$> infinit-volume --create --as alice --network cluster --name shared
Locally created volume "alice/shared".
```

_**NOTE**: You may have noticed that the name of the network is sometimes prepended with the username of its owner e.g "alice/cluster". This fully-qualified name distinguishes objects that you own from the ones that you don't. When manipulating objects of which you are the owner, you will not need to use the fully-qualified name as the command-line tools will automatically search in the user's namespace._

### Publish a volume ###

A volume often needs to be shared with the other users in the network. As with the other resources, the easiest way to do this is to rely on the Hub, either using the `--push` option on volume creation or by publishing the volume as a separate action:

```
$> infinit-volume --push --as alice --name shared
Remotely pushed volume "alice/shared".
```

_**NOTE**: You may want to keep your volume hidden from the users on a network, in which case you could omit this step and distribute its descriptor using the `--export` and `--import` options._

### List the volumes ###

You can list the volumes that you have local descriptors for with the `--list` option. Remember that you can also fetch the volumes on your networks which have been published to the Hub:

```
$> infinit-volume --fetch --as alice
Fetched volumes for user "alice".
$> infinit-volume --list
alice/shared: network alice/cluster
```

### Mount a volume ###

Mounting an Infinit volume is very similar to mounting any other file system. As usual, there are two ways to achieve this, either by relying on the Hub as a helper or to do it manually in which case you will need to specify the host and port of some bootstrap nodes for your device to discover the nodes in the network.

Note that if you have been invited to join the network, you will need to fetch the volume before being able to mount it. Refer to the <a href="#list-the-volumes">List the volumes</a> section in this case.

The following command mounts an Infinit file system. Note that the `--publish` option tells the binary to rely on the Hub to ease the process of connecting to the underlying network by providing you with the endpoint of bootstrap nodes while publishing your own endpoint for other nodes to find you as well:

```
$> infinit-volume --mount --as alice --name shared --mountpoint /mnt/shared/ --publish
Fetched endpoints for "alice/cluster".
Running network "alice/cluster".
Remotely pushed endpoints for "alice/cluster".
Running volume "alice/shared".
...
```

The `--mountpoint` option could be omitted if a default mount point was provided at the volume's creation.

_**NOTE**: There are a number of options that can be used to alter the behavior of the file system such as determining the size of the cache, activating asynchronous operations for better performance, etc. Invoke the `--help` option to learn more._

When using a completely decentralized environment, the `--peer` option provides the binary one or more bootstrap nodes needed to discover the underlying network.

Once the volume is mounted, one can interact with the file system through common UNIX built-ins and binaries as for any other file system:

```
$> ls /mnt/shared/
$> echo "everything is" > /mnt/shared/awesome.txt
$> cat /mnt/shared/awesome.txt
everything is
```

**IMPORTANT**: It is possible that the volume owner didn't grant you access to the root directory, in which case you would get a "Permission Denied" error when listing the mount point. In this case, request that the volume owner <a href="#grant-revoke-access">grant's you access</a>.

Access Control List
--------------------------

Having joined a volume does not necessarily mean that you have the required permissions to browse the files and directories in it. As in most file system, in order to access, edit and/or delete a file, its owner must first grant you the permission to do so.

Unlike many file systems, Infinit provides advanced decentralized (i.e. without relying on a centralized server) access control mechanisms that allow any user to manage permissions on his/her files and directories.

Being the owner of a volume automatically grants you access to its root directory. It is then your responsibility to manage the permissions on the root directory for other users to use the volume.

Note that most access control actions use POSIX mechanisms such as standard permissions and extended attributes. In particular, the `infinit-acl` can be considered as a wrapper on top of extended attributes.

### Grant/revoke access ###

In order to grant or revoke access to a file or directory, one uses the _infinit-acl_ binary, providing the path to the object to manage and the permissions to apply.

The following grants Bob the permissions required to read and write the root directory of Alice's volume mounted to `/mnt/shared/`.

```
$> infinit-acl --set --path /mnt/shared/ --mode rw --user bob
```

_**NOTE:** The infinit-acl binary provides additional options to better manage hierarchical permissions. Do not hesitate to rely on the help to learn more._

Once the command has been run, Bob will be able to read and write files/directories in the root directory of Alice's 'shared' volume.

#### Inheritance ####

ACL inheritance is a mechanism that sets the ACL of newly created files and directories to the ACL of their parent directory. It can be enabled or disabled on a per-directory basis using the `--enable-inherit` and `--disable-inherit` options:

```
$> infint-acl --set --path /mnt/shared --enable-inherit
```

If ACL inheritance is disabled, newly created files and directories can only be accessed by their owner. If enabled, all the ACLs set on the parent directory are copied to the new object, including the inheritance flag for directories.

#### World-readability/writability ####

By default, files and directories can only be read/written by users present in the object's ACLs. It is possible to flag a file/directory as world-readable (everyone can read it) or world-writable (everyone can modify it).

The _chmod_ UNIX binary must be used to that effect. The following example sets a file as world-readable before making it world-writable as well. One can notice that the `ls -l` command displays a file as world-readable/write through the _others_ category (the last three `rwx` indicators).

```
$> ls -l /mnt/shared/awesome.txt
-rw-------  1 alice  users     14B Jan 20 16:55 awesome.txt
$> chmod o+r /mnt/shared/awesome.txt
-rw----r--  1 alice  users     14B Jan 20 16:55 awesome.txt
$> chmod o+w /mnt/shared/awesome.txt
-rw----rw-  1 alice  users     14B Jan 20 16:55 awesome.txt
```

### List permissions ###

Every user with the volume descriptor and the right permissions can consult the Access Control List (ACL) associated with a file system object:

```
$> infinit-acl --list --path /mnt/shared/awesome.txt
/mnt/shared/awesome.txt:
     alice: rw
     bob: r
```

#### POSIX mode ####

Since the Infinit access model is ACL based, the POSIX file mode as displayed by _ls -l_ differs from what you might expect:

```
$> ls -l /mnt/shared/
total 64
drwx------  1 alice  staff     0B Jan 20 17:15 Engineering
-rw-------  1 alice  staff    14B Jan 20 16:59 awesome.txt
```

Indeed, one must take into account that:

- User and group IDs are set to the user who mounted the file system if he/she has read or write access to the file. Otherwise they are set to root. Changing them (using _chown_) has no effect.
- User read/write access mode (u+r and u+w) are set according to the ACLs, properly reflecting what operations the user having mounted the file system is allowed to perform. Changing those flags has no effect.
- User execute access mode can be set or cleared and is preserved. Noteworthy is that this protection is not ensured at the network level through cryptographic mechanisms as it is the case for read and write. Instead, a flag is just set to indicate that the file is 'executable'.
- Group modes are irrelevant and set to zero.
- Others read/write access mode can be set to make the object readable/writable for all. See <a href="#world-readability-writability">World-readability/writability</a> for more information.

### Create a group ###

Infinit supports the concept of group i.e a collection of users. Such groups ease the process of access control management by allowing a user to re-reference groups of users it has previously created/used.

A group is identified by it's unique name, and can be created by any user in the network. It stores a list of group members that can be users and other groups, leading to hierarchical groups. This member list can be modified only by the users managing the group, by default only the user who created it. Below is shown an example of group creation:

```
$> infinit-acl --group --create --name marketing --path .
```

_**NOTE**: The `--path` option must be provided for the infinit-acl to know which volume, hence network, you want the group to be created in. You can use the `--path` option to reference the volume's mountpoint or any of its files/folders._

From that point, it is very easy to display information on a group through the `--show` action:

```
$> infinit-acl --group --show --name marketing --path .
{"admins":["alice"],"members":["@marketing","alice"]}
```

_**NOTE**: Since the `--path .` option is provided last, it could simply be replaced by `.`: `infinit-acl --group --show --name marketing .`._

Once created, a group can be added to any object's ACLs using _infinit-acl --set_. The process is similar to granting access to a user except that the group name must be prefixed with an '@':

```
$> infinit-acl --set --mode rw --user @all --path /mnt/shared/awesome.txt
```

### Add/remove group members ###

Any group administrator can add and remove members through the `--add` and `--remove` options. In the example below, Alice first adds Bob as a member of her Marketing group. Then, Alice creates a group named 'marketing/tokyo' and adds it to her Marketing group.

```
$> infinit-acl --group --name marketing --add bob --path .
$> infinit-acl --group --create --name marketing/tokyo --path .
$> infinit-acl --group --name marketing --add @marketing/tokyo --path .
```

_**NOTE**: One can notice that groups are referenced by prepending the '@' symbol to the group name._

### Add/remove group administrators ###

A group can be administered by multiple users at once, increasing the flexibility of the group concept a bit more. To add/remove administrator to a group, simply rely on the `--admin-add` and `--admin-remove` actions:

```
$> infinit-acl --group --name marketing --admin-add bob --path .
$> infinit-acl --group --show --name marketing --path .
{"admins":["alice","bob"],"members":["@marketing","alice","bob"]}
```

Device
---------

### Log in on another device ###

You may wish to access your file systems from another machine. The critical nature of the user's identity (which is similar to an SSH key) makes this operation more complex than the others.

In a nutshell, one needs to re-create his/her Infinit environment with all the resources (users, networks, volumes, drives etc.) on the other computer. If you are using Infinit in a completely decentralized manner, then the operation of exporting all the objects manually and re-importing them on the other device will be familiar. If you have gotten used to the ease-of-use of the Hub, then we offer you two methods to transmit your user identity to another device.

Note that the preferred method when using the command-line tools should be to <a href="#transmit-user-identity-to-another-device">transmit the user identity</a> to another device.

#### Store the user identity on the Hub ####

The easiest (but least secure) way to retrieve your user identity on another device is to activate a mode in which your private identity is kept on the Hub, encrypted with a key to protect it from potential intruders.

To activate this mode, you need to specify the `--full` option when signing up on the Hub, along with a password, as shown below. Note that the password can be provided in-line using the `--password` option or entered when prompted:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-user --signup --name alice --email alice@company.com --fullname Alice --full
Password: ********
Remotely pushed user "alice".
</code>
</pre>

Following this operation, one can login on another device very simply by invoking the `--login` option:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --login --name alice
Password: ********
Locally saved user "alice".
</code>
</pre>

That's it, you can see by listing the local users that your private user identity has been retrieved:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --list
alice: public/private keys
</code>
</pre>

_**NOTE**: If you already registered your user on the Hub but would like to activate this login mode, you can remove your user from the Hub with the `--pull` option and then re-`--push` with the `--full` option along with a password._

#### Transmit user identity to another device ####

If you are uncomfortable with us keeping your user identity, there is another (preferred) way for you to transmit your user identity to another machine.

The method relies on the Hub as a temporary store for your user identity to be kept until it is retrieved on another device. If not retrieved after 5 minutes, the user identity will be removed from the Hub. The user identity is also encrypted with a key of your choice so that we cannot access it. In the future, a direct point-to-point method will be used to bypass the Hub altogether.

<pre>
<div><span>Device A</span></div>
<code>$> infinit-device --transmit --as alice --user
Passphrase: ********
Transmitted user identity for "alice".
User identity on the Hub for: 297 seconds
</code>
</pre>

Once the command has been launched, you have 5 minutes to retrieve the user identity on another device using the following command:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-device --receive --user --name alice
Passphrase: ********
Received user identity for "alice".
</code>
</pre>

You can verify that the user has been saved locally by listing the local users:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --list
alice: public/private keys
</code>
</pre>

Once retrieved, the user identity is removed from the Hub. Even though this method is not ideal, it is a fair compromise between security (short window of attack, encrypted with a unique key of your choosing etc.) and simplicity (two commands to invoke, no file to manually move around etc.).

#### Manually export/import the user identity ####

For users that either do not trust the two methods above or who are using a completely decentralized environment, there is another method which can be used. This requires exporting the full user identity, moving the file manually and finally re-importing it on the new device.

First, export the user identity to a file:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-user --export --name alice --full --output alice.user
WARNING: you are exporting the user "alice" including the private key
WARNING: anyone in possession of this information can impersonate that user
WARNING: if you mean to export your user for someone else, remove the --full flag
Exported user "alice".
$> cat alice.user
{"email":"alice@company.com","fullname":"Alice","id":"2J8reEAY","name":"alice","private_key":{"rsa":"MIIEp...M/w=="},"public_key":{"rsa":"MIIBC...DAQAB"}}
</code>
</pre>

At this point, it is your responsibility to move the file to your other device, using _SCP_ for instance. Re-creating the user entity the just requires an import:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --import --input alice.user
Imported user "alice".
</code>
</pre>

Drive
-------

Once you've created your storage infrastructure comprising of a network, storage resources and volumes, you may wish to invite other users, potentially non-tech-savvy, to use it to seamlessly store and access their files.

A client application with a graphical interface called <a href="http://infinit.sh/drive">Infinit Drive</a> is provided for end-users to see the drives they are allowed to access, the people contributing to it, their permissions, etc.

<img src="${url('images/desktop-client.png')}" alt="Infinit Drive app">

The notions of storage resources, networks and volumes are too technical for most end-users. Such users may also require a simple email guiding them through the set-up process.

This is why the notion of a *drive* has been introduced. A drive is nothing more than an abstraction on top of a volume.

Rather than listing all the networks and volumes a user is allowed to access in the graphical interface, only the drives the user has been invited to join will be shown. This way, should an administrator create a storage network with hundreds of volumes, the end-user will only see the drives they have been explicitly given access to, making his/her experience as enjoyable as possible without limiting the possibilities of the underlying command-line tools.

### Create a drive ###

Creating a drive is as easy as any other operation. The following creates a drive named "workspace" based on the network "cluster" and volume "shared".

```
$> infinit-drive --create --as alice --network cluster --volume shared --name workspace --description "Alice's, Bob's and Charlie's workspace" --push
Locally created drive "alice/workspace".
Remotely pushed drive "alice/workspace".
```

Note that the `--push` option is included to publish the drive to the Hub so that it is easily retrievable by the other users, in particular the ones that we will be <a href="#invite-users">inviting</a> to join.

### List the drives ###

As for the other resources, one can very simply list the local drives. Note that refreshing the local snapshots can be achieved with the `--fetch` action:

```
$> infinit-drive --fetch --as alice
Fetched drives for user "alice".
$> infinit-drive --list
alice/workspace: ok
```

### Invite users ###

It is now time to invite users to join the drive you've created for them.

Note that before you can reference a user, you need to fetch his/her public identity using the `infinit-user --fetch` command. Likewise, every user that will be invited must have been issued a passport to connect to the network.

When inviting users, you can use the `--passports` option to automatically create any passports that are needed for the users you are inviting to the drive. The sequence of commands below shows how to invite both Bob and Charlie. The user Bob has already been fetched while Charlie is a completely new user, that needs to be fetched.

```
$> infinit-user --fetch --as alice --name charlie
Fetched user "charlie".
$> infinit-drive --invite --as alice --name workspace --user bob --user charlie --passports --push
Locally created passport "alice/cluster: charlie".
Locally created invitation for "bob".
Locally created invitation for "charlie".
Remotely pushed passport "alice/cluster: charlie".
Remotely pushed invitations "alice/workspace: bob, charlie".
```

That's it, Bob and Charlie have been invited to join the drive named "alice/workspace". Following the `--push` of the invitations, an email is sent to notify each invited user of their invitation and letting them know how to proceed.

If you would like to prepare invitations locally and push them all later, you can do this by omitting the option `--push` in the previous sequence of commands and later call the command as shown below:

```
$> infinit-drive --invite --as alice --name workspace --push
Remotely pushed invitations "alice/workspace: bob, charlie".
```

Without any `--user` specified the `--invite` command will push each pending invitations to the Hub, sending the notification emails as a consequence.

### Join a drive ###

Even though the drive abstraction has been introduced for users of graphical interface, you can decide to join a drive through the command-line tools.

First, remember to update your local drive descriptors by fetching the drives you've been invited to and already have access to. Once fetched, you can list the drives you have locally.

```
$> infinit-drive --fetch --as charlie
Fetched drive for user "charlie"
$> infinit-drive --list --as charlie
alice/workspace
```

You can then join a drive using the following command:

```
$> infinit-drive --join --as charlie --name alice/workspace
Joined drive "alice/workspace".
```

That's it, you are now allowed to mount the volume (i.e. 'alice/shared') associated with the drive to browse, store and access files. Note that you could have done that without using through the drive invitation process because you are using the command-line tools. Non-tech-savvy users, however, will appreciate having an interface with only the drives they have been invited to join and thus have access to.
