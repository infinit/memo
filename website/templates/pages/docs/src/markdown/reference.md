Reference
========

This document goes through the command-line tools, describing how to perform specific tasks such as creating a storage network, contributing storage capacity, inviting users to join a drive and more.

Introduction
----------------

### Terminology ###

The Infinit command-line tools are composed of several binaries, each dealing with a specific resource/object.

A *user* represents the entity performing operations on files, directly or indirectly. Every user possesses a RSA key pair that is used to identify him/her. A user can create a *network* which represents the interconnection of computing devices that will compose the storage infrastructure. A *storage* is a storage resource, local or remote, that can be connected to a device to support part of the overall storage load. Finally, several *volume*s --- i.e logical drives ---  can be created within a network.

The *hub* is a cloud service whose role is to ease the process of discovery, sharing and more.

### Home ###

All the configuration files Infinit creates and uses are located in the `$INFINIT_HOME` directory which, by default, is set to `$HOME/.local/share/infinit/filesystem/`.

One can very easily set the environment variable to point to the directory of his/her choice, either by modifying the shell configuration or by setting it manually:

```
$> export INFINIT_HOME="/some/where/"
```

### Nomenclature ###

All the command-line tools rely on the same terminology when it comes to options and arguments. As an example, most binaries have options named `--create`, `--list`, `--fetch` etc.

For more information on the options provided by a binary, do not hesitate to rely on the `--help` option. Noteworthy is that the help is relative to the options already provided.

As an example, the help for _infinit-user_ displays the general options available: export, create, import, list etc.:

```
$> Usage: infinit-user MODE [OPTIONS...]

Infinit user utility:

Modes:
  --create               Create a user
  --export               Export a user so that it may be imported elsewhere
  --fetch                Fetch a user from the Hub
  --import               Import a user
  --pull                 Remove a user from the Hub
  --delete               Delete a user locally
  --push                 Push a user to the Hub
  --signup               Create and register a user
  --login                Log the user to the Hub
  --list                 List users

Miscellaneous:
  -h [ --help ]          display the help
  -s [ --script ]        silence all extraneous human friendly messages
  -v [ --version ]       display version
```

However, the help when invoking the `--create` option gives a completely different output:

```
$> Usage: infinit-user --create [OPTIONS...]

Create a user:

Create options:
  -n [ --name ] arg      user name (default: system user)
  -k [ --key ] arg       RSA key pair in PEM format - e.g. your SSH key
                         (default: generate key pair)
  --push-user            push the user to the Hub
  -p [ --push ]          alias for --push-user
  --email arg            valid email address (mandatory when using --push-user)
  --fullname arg         user fullname (optional)
  --full                 include private key in order to facilitate device
                         pairing and fetching lost keys.
  --password arg  password to authenticate with the Hub. Used with
                         --full (default: prompt for password)
```

Every binary follows the same semantic with the first option representing the mode of operation (a verb): `--create`, `--pull`, `--list`, `--delete`, `--export` etc.

The name/identifier of the object on which you which to operate can be specified through the `--name` option or simply by providing it outside of any option. As such both lines below are equivalent:

```
$> infinit-volume --push --name personal
$> infinit-volume --push personal
```

Except for the _infinit-user_ binary, one can specify the Infinit user behind the action by relying on the `--as` option followed by the user name. If not specified, the `$INFINIT_USER` environment variable is used, unless not set in which case the system user name is used.

Finally, some command-line tools, in particular _infinit-network_, and _infinit-volume_ can be "run". As such, they have options such as `--start`, `--stop`, `--status` etc. in order to allow developers to manipulate them in a similar fashion to other UNIX daemons e.g init scripts.

### Hub ###

All objects (users, storages, networks, volumes etc.) are created locally by default with no server involved. The creation process may generate one or more files and store them in the `$INFINIT_HOME` directory.

The command-line tools however provide a way to rely on the hub for certain operations in order to simplify some administrative tasks such as inviting a user to a drive, sharing the volumes created within a network, moving the user identity to another of your devices and so on. In addition, some functionalities such as the consumption of storage capacity in a network are only available through the hub. As a rule of thumb, we advise you to always rely on the hub, unless you know exactly what you are doing.

The use of the hub can be activated through specific options, mostly `--push`, `--fetch` and `--pull`. The `--push` option publishes an object on the hub for other users to retrieve it. The `--pull` option does the exact opposite, removing the object from the hub. Finally, the `--fetch` option retrieves a resource from the hub, e.g a network descriptor, and stores it locally in the `$INFINIT_HOME` directory.

One can decide to either create objects locally before pushing them to the hub or to perform both tasks through a single action by specifying `--push` option when invoking the command.

Note that some binaries operate in hub mode by default. For instance the _infinit-drive_ binary would not make sense without the hub since its role is to bridge the gap between a low-level storage infrastructure and potential non-tech-savvy users.

**IMPORTANT**: This document covers mainly flows involving the hub. For users wanting to use Infinit in a pure decentralized environment, just know that `--push`/`--fetch` operations must be replaced by `--export`/`--import` operations while the files must be manually shared with other users and moved between devices. Also, you will sometimes need to provide additional information such as the IP address of bootstrap nodes in order to discover the other nodes of a network.

User
------

The _infinit-user_ binary allows one to create a user identity, publish it on the hub to be referenced by other users and so on and so forth.

### Create a user ###

A user is not much more than a RSA key pair that will be used to sign and encrypt data. The following creates a user, automatically generating a new RSA key pair.

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
Passphrase: ********
Locally generated user "alice".
```

_**WARNING**: Keep in mind that your Infinit user identity must never fall into someone else’s hands. The user identity file is the equivalent of an SSH key file and must therefore be kept private at all times._

### Sign up on the hub ###

To register on the hub, you can either use the `--push` option when creating your user, push the user once it has been created locally or sign up directly on the hub.

To push an existing user, simply invoke the _infinit-user_ with the `--push` mode and `--email` option to specify a valid email address. Needless to say that this email address will *never* be shared with third parties and will solely be used for Infinit to communicate news of its file storage platform.

**IMPORTANT**: Given the critical nature of the user identity, we strongly advise you to read the <a href="#log-in-on-another-device">Log in on another device</a> section in order to completely understand the ramifications of the options used when pushing your user.

```
$> infinit-user --push --name alice --fullname "Alice" --email alice@company.com
Remotely pushed user "alice".
```

Unfortunately, since names are unique, your user name may already be taken on the hub, in which case the operation will fail. The action `--signup` has been introduced to overcome this problem, performing the equivalent of `--create --push` atomically, making sure that the user is created locally and remotely at once.

We advise users to sign up to the hub very early on in order to avoid future complications:

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

The list of users kept locally can contain both user identities that you created and therefore own but also public identities of users that you fetched from the Hub for instance.

```
$> infinit-user --list
alice: public/private keys
bob: public key only
```

Credentials
---------------

The _infinit-credentials_ binary manages the credentials to your cloud services. Indeed, cloud services can be added to be later plugged to a network as storage resources.

The cloud services can be of very different nature, being storage service at the block level such as the AWS S3 object store service, at the file level such as Dropbox or Google Drive, even services that store data item of a specific nature such as images with Flickr. Basically anything that allows writing, editing, finding and deleting a piece of information.

Infinit considers these cloud services as basic and unprivileged datastores that are used to store blocks of encrypted data.

*__NOTE__: Because this binary exclusively works with the hub, you must have registered your user to the Infinit hub to be able to manage your credentials. For more information, please refer to the <a href="#user">User</a> section, more specifically how to <a href="#sign-up-on-the-hub">Sign up on the hub</a>.*

### Add credentials ###

To add a cloud service such as Dropbox, just use the `--add` action option. Note however that depending on the cloud service to add, the options may vary along with the authentication process. Please refer to the help for more information.

```
$> infinit-credentials --add --as alice --dropbox
Register your Dropbox account with Infinit by visiting https://…
$>
```

Just follow the instructions by visiting the URL provided in order to authenticate yourself and authorize Infinit to access your account. At this point, you should be automatically redirected to the infinit.sh website.

_**NOTE**: Do not worry, Infinit will never alter the existing data items stored in your cloud services; it will only create blocks of encrypted data in a subfolder specific to Infinit._

Once a cloud service added to your account, you can fetch the new credentials on your device through the `--fetch` option:

```
$> infinit-credentials --fetch --as alice
Fetched Dropbox credentials 51218344 (Alice)
Fetched AWS S3 credentials alice@company.com (Alice)
```

### List credentials ###

At any point, you can list your local credentials through the `--list` option.

Note that this list may be outdated, in which case you may want to update it by fetching the most recent credentials from the hub:

```
$> infinit-credentials --fetch --as alice
$> infinit-credentials --list
Dropbox:
  51218344 (Alice)
AWS S3:
  alice@company.com (Alice)
```

Storage
-----------

The _infinit-storage_ binary allows for the definition of storage resources. Such storage resources can be local — storing blocks of data on the local file system, on a partition or in a database — or remote in which case the blocks of data are stored through a cloud service API.

Noteworthy is the storage resources are device-specific. As such, such resources cannot be pushed to the hub since they only live locally.

### Create a storage resource ###

#### Locally ####

To create a storage resource on top of a local file system, simply specify the `--filesystem` option. Note that you can also specify, through the `--path` option, where the encrypted data blocks will be stored.

```
$> infinit-storage --create --filesystem --capacity 2GB --name local
Created storage "local".
```

#### Remotely ####

You can create a storage on top of a cloud service API. Obviously, you will need to add this cloud service through the _infinit-credentials_ first, as shown in the <a href="#add-credentials">Add credentials</a> section.

All you need to do then is specify the type of cloud service you want your storage to rely upon along with the cloud service account identifier. Cloud service identifiers can be retrieved when <a href="#list-credentials">listing your credentials</a>.

The following creates a storage resource on top of Dropbox, specifying the account identifier and a name for the storage:

```
$> infinit-storage --create --dropbox --account 51218344 --name dropbox
Created storage "dropbox".
```

Note that the list of supported cloud services can be retrieved through the `--create --help` options. In the future, more of those will be supported, from <a href="https://aws.amazon.com/s3">AWS S3</a>, <a href="https://cloud.google.com/storage">Google Cloud Storage</a>, <a href="https://www.backblaze.com/b2">Backblaze B2</a> and many more.

Network
-----------

Through the _infinit-network_ you are going to be able to create overlay networks, configure the the way the distributed hash table behaves and much more.

### Create a network ###

The example below creates a network named _‘cluster’_ which aggregates the storage resources controlled by the users involved in this network.

The network can be configured depending on the requirements of the storage infrastructure the administrator is setting up. For instance, the number of computing devices could be extremely small, the owners of those computers could be somewhat untrustworthy or the machines could be expected to be turned off and on on a daily basis following their work schedule. All these parameters can be used to tune the network: the overlay’s topology, the replication factor, the fault tolerance algorithm and much more.

The following creates a small storage network, relying on the Kelips overlay network with a replication factor of 3. In addition, the administrator decides to contribute two storage resources to the network in creation.

```
$> infinit-network --create --as alice --kelips --k 1 --replication-factor 3 --storage local --storage dropbox --name cluster
Locally created network "alice/cluster".
```

### Publish a network ###

You can now publish a network for other users to retrieve it. Note that the easiest way is always to append the `--push` option to the network creation command to perform both creation and publications actions at once.

Otherwise, you can push the network to the hub through the `--push` action option, as usual:

```
$> infinit-network --push --as alice --name cluster
Remotely pushed network "alice/cluster".
```

As for every other resource, you can decide to manipulate networks without relying on the hub. Please refer to the `--export` and `--import` options in this case.

### List the networks ###

You can easily list the networks that you have locally through the `--list` mode. Do not forget that you may need to fetch the networks from the hub in order to be in sync:

```
$> infinit-network --fetch --as alice
Fetched networks for user "alice".
$> infinit-network --list
alice/cluster
```

### Join a network ###

Let us say that you want to connect a device to a network, this device being different from the one on which the network has been created.

There are two ways to do this depending on who you are related to the network: its owner or an invited user.

#### As the owner ####

As the owner of the network, the system automatically recognizes you and allows you to join the network on other devices. The process in this case is pretty straightforward.

Note however that when joining a network, you can decide to contribute storage from this new device. In the example below, Alice is going to connect one of her other devices, taking care to contribute storage capacity from her personal Network-Attached Storage (NAS).

```
$> infinit-network --join --as alice --name cluster --storage nas
Locally joined network "cluster".
```

_**NOTE**: Keep in mind that the action of joining a network must be performed on every new device.

#### As an invitee ####

In this case, you should have <a href="#receive-a-passport">received</a>, through the hub or manually, an invitation to join a network created by someone else. Such invitations are called _passports_ and allow users to connect their devices to the network they’ve been allowed to join.

In order to join a network, you must first retrieve its descriptor. This network descriptor can be fetched from the hub through a single command:

```
$> infinit-network --fetch --as bob --name alice/cluster
Fetched network "alice/cluster".
```

You now have locally both the network descriptor and a passport allowing you to connect new devices to it. Let’s join Alice’s ‘cluster’ network with Bob’s current device. Note that one can decide to contribute additional storage capacity through the `--storage` option.

```
$> infinit-network --join --as bob --name alice/cluster
Locally joined network "alice/cluster".
```

_**NOTE**: This process must be repeated on every device, proving that Bob has indeed been allowed to join the network (via the passport) and that this device belongs to Bob.

Passport
------------

The _infinit-passport_ binary can be used to allow other users to join one’s networks, granting them the right to connect devices, contribute storage resources and potentially access files.

### Create a passport ###

In order to allow another user to join a network, you must issue a passport. In order to reference the user to invite, you first need to obtain his/her user public identity.

Let’s say that you want to invite the user ‘bob’ to your network. First you need to fetch his identity from the hub (or retrieve it manually if operating without the hub, see `--export` and `--import`):

```
$> infinit-user --fetch --as alice --name bob
Fetched user "bob".
```

The passport that you are about to create will be sealed, allowing only Bob to <a href="#join-a-network">connect new devices</a> to the network:

```
$> infinit-passport --create --as alice --network cluster --user bob
Locally created passport "alice/cluster: bob".
```

Now that the passport has been created, read the <a href="#distribute-a-passport">Distribute a passport</a> section to learn how to distribute it to the invited user.

### List the passports ###

You can list both the passports you’ve created for other users to join your networks and the passports issued by other users for you to join their networks:

```
$> infinit-passport --list --as alice
alice/cluster: bob
```

### Distribute a passport ###

Once the passport locally created, you must distribute it to the invited user for him/her to be able to join your network.

The easiest way to do that is to rely on the hub, by appending the `--push` option when creating the passport; see the <a href="#create-a-passport">Create a passport</a> section.

You can otherwise push a local passport by invoking the `--push` action option as shown below:

```
$> infinit-passport --push --as alice --network cluster --user bob
Remotely pushed passport "alice/cluster: bob".
```

Should you be evolving in a pure decentralized environment i.e without the hub, you will need to manually export the passport and transmit it to the invited user in which case you should refer to the `--export` and `--import` options.

**IMPORTANT**: Be aware that the invited user will not be notified of that fact that you’ve allowed him/her to join your network. The invited user could detect it by fetching his passport and noticing a new one but that’s about it. In order to speed things up, you should probably tell him through the medium of your choice: chat, email, carrier pigeon or else.

### Receive a passport ###

You can very easily fetch your passports from the hub in order to refresh your local snapshots:

```
$> infinit-passport --fetch --as bob
Fetched passports for user "bob"
$> infinit-passport --list --as bob
alice/cluster: bob
```

*__NOTE__: The _infinit-passport_ binary provides options to specifically fetch passports from a certain user or for a specific network.*

That’s it, you will now be able to <a href="#join-a-network">join the networks</a> these passports allow you to.

Volume
----------

On top of the storage layer i.e the network, one can create a file system also known as logical drive or volume. A volume is represented by the address of its root directory. While centralized file systems store this address in a specific block known as the _superblock_, Infinit simply stores it in a file located in the `$INFINIT_HOME` directory which describes the volume.

Note that several volumes can be created within the same network, in which case you could see those as partitions on the same hard disk drive.

### Create a volume ###

The command below creates a volume in a network. You can specify the default mount point for the volume through the `--mountpoint` option even though the following example does not:

```
$> infinit-volume --create --as alice --network cluster --name shared
Locally created volume "alice/shared".
```

_**NOTE**: You may have noticed that the name of the network is sometimes prepended with the username of its owner e.g alice/cluster. This fully-qualified name helps distinguish objects that you own from the ones that you don’t. However, in most cases, you will not have to specify the fully-qualified name as the command-line tools are smart enough to deduce the object you are trying to reference._

### Publish a volume ###

A volume often needs to be shared with the other users in the network. As for the other resources, the easiest way to perform this action is to rely on the hub, either by using the `--push` option at the volume creation or by publishing the volume as a separate action:

```
$> infinit-volume --push --as alice --name shared
Remotely pushed volume "alice/shared".
```

_**NOTE**: You may however want to keep your volume private in a network which several users contribute storage capacity to, in which case you could omit this step._

### List the volumes ###

You can very easily list the volumes that you have locally through the `--list` mode. Remember that you can also fetch the volumes of your networks and the ones you’ve been invited to:

```
$> infinit-volume --fetch --as alice
Fetched volumes for user "alice".
$> infinit-volume --list
alice/shared: network alice/cluster
```

### Mount a volume ###

Mounting an Infinit volume is very similar to mounting any other file system. As usual, there are two ways to achieve this, either by relying on the hub as a helper or to do it manually in which case you will need to specify the host and port of some bootstrap nodes for your device to discover the nodes in the network.

Note that if you have been invited to join the network, you will need to fetch the volume before being able to mount it. Refer to the <a href="#list-the-volumes">List the volumes</a> section in this case.

The following command mounts an Infinit file system. Note that the `--publish` option tells the binary to rely on the hub to ease the process of connecting to the underlying network by providing you with the endpoint of bootstrap nodes while publishing your own endpoint for other nodes to find you as well:

```
$> infinit-volume --mount --as alice --name shared --mountpoint /mnt/shared/ --publish
Fetched endpoints for "alice/cluster".
Running network "alice/cluster".
Remotely pushed endpoints for "alice/cluster".
Running volume "alice/shared".
...
```

The `--mountpoint` option could be omitted should a default mount point had been provided at the volume creation.

_**NOTE**: There are a number of options that can be used to alter the behavior of the file system such as determining the size of the cache, activating asynchronous operations for better performance and many more. Please invoke the `--help` option to learn more._

If evolving in a completely decentralized environment, you will need to rely on the `--peer` option to provide the binary one or more bootstrap nodes to discover the underlying network.

At this point, one can interact with the file system through common UNIX built-ins and binaries as for any other file system:

```
$> ls /mnt/shared/
$> echo "everything is" > /mnt/shared/awesome.txt
$> cat /mnt/shared/awesome.txt
everything is
```

**IMPORTANT**: It is possible that the volume owner didn’t grant you access to the root directory, in which case you should get a "Permission Denied" error when listing the mount point. In this case, request the volume owner to <a href="#grant-revoke-access">grant you access</a>.

Access Control List
--------------------------

Having joined a volume does not necessarily mean that you will have the permission to browse the files and directories in it. As in most file system, in order to access, edit and even delete a file, its owner must first grant you the permission to do so.

Unlike many file systems, Infinit provides advanced decentralized (i.e without relying on a centralized server) access control mechanisms that allow any user to manage permissions on his/her files and directories.

Note that being the owner of a volume automatically grants you access to its root directory. It is then your responsibility to manage the permissions on the root directory for other users to use the volume.

### Grant/revoke access ###

In order to grant or revoke access to a file or directory, one simply has to rely on the _infinit-acl_ binary, providing the path to the object to manage and the permissions to apply.

The following grants Bob the permissions to read and write the root directory of Alice’s volume named ‘shared’.

```
$> infinit-acl --set --path /mnt/shared/ --mode rw --user bob
```

**_NOTE_: Note that the _infinit-acl_ binary provides additional options to better manage hierarchical permissions. Do not hesitate to rely on the help to learn more.**

From that point, Bob will be able to access the volume to read and write files/directories in the root directory of Alice's 'shared' volume.

### List permissions ###

Every user with the volume descriptor and the right permissions can very easily consult the Access Control List (ACL) associated with a file system object:

```
$> infinit-acl --list --path /mnt/shared/awesome.txt
/mnt/shared/awesome.txt:
     alice: rw
     bob: r
```

Device
---------

### Log in on another device ###

You may want to access your file systems from another machine. Even though it sounds easy, the critical nature of the user’s identity, similar to a SSH key, makes it a bit more complicated that all the other operations.

In a nutshell, one needs to re-create his/her Infinit environment with all the resources (users, networks, volumes, drives etc.) on the other computer. If you are already using Infinit in a completely decentralized manner, then the operation of exporting all the objects manually and re-importing them on the other device should not frighten you. If you have gotten used to the ease-of-use of the hub, then we offer you two methods to transmit your user identity on another device.

Note that the preferred method when using the command-line tools should be to <a href="#transmit-user-identity-to-another-device">transmit the user identity</a> to another device.

#### Keep the user identity ####

The easiest (least secure) way to retrieve your user identity on another device is to activate a mode in which your private identity is kept on the hub, encrypted with a key to protect it from potential intruders.

To activate this mode, you need to specify the `--full` option when signing up on the hub, along with a password, as shown below. Note that the password can be provided through the `--password` option or entered when prompted:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-user --signup --name alice --email alice@company.com --fullname Alice &#8211;&#8211;full
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

That’s it, you can see by listing the local users that your private user identity has been retrieved:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --list
alice: public/private keys
</code>
</pre>

_**NOTE**: If you already registered your user on the hub but would like to activate this login mode, you can remove your user from the hub through the `--pull` option and then re-`--push` it taking care to provide the `--full` option along with a password._

#### Transmit user identity to another device ####

If you are uncomfortable with us keeping your user identity (we are! you should!), there is another (preferred) way for you to just transmit your user identity to another machine.

Just know that for now, the method does rely on the hub as a temporary place for your user identity to be kept until it is retrieved on another device. Do not worry though, if not retrieved after 5 minutes, it will be removed from the hub. Besides, your user identity is encrypted with a key of your choice so we cannot access it. In the future, a direct point-to-point method will be used to bypass the hub altogether.

<pre>
<div><span>Device A</span></div>
<code>$> infinit-device --transmit --as alice --user
Passphrase: ********
Transmitted user identity for "alice".
</code>
</pre>

From that point, you have 5 minutes to retrieve the user identity on another device through the following command:

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

Once retrieved, the user identity is wiped out from the hub to make sure nobody can try anything. Even though this method is not ideal yet, it is a fair compromise between security (short window of attack, encrypted with a unique key of your choosing etc.) and simplicity (two commands to invoke, no file to manually move around etc.).

#### Manually export/import the user identity ####

For users that either do not trust any of the two methods above or want to evolve in a completely decentralized environment, there is another method which consists in exporting, moving the file manually and finally re-importing the user identity on the new device.

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

At this point, it is your responsibility to move the file to your other device, through _SCP_ for instance. Re-creating the user entity is then just one step away:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-user --import --input alice.user
Imported user "alice".
</code>
</pre>

Drive
-------

Once you’ve created a storage infrastructure with a user, a network, storage resources and volumes, it may be time to invite other users, potentially non-tech-savvy, to use it to seamlessly store and access their files.

A client application called <a href="http://infinit.sh/drive">Infinit Drive</a> is provided for end-users to see the drives they are allowed to access, the people contributing to it, their permissions etc.

<img src="${url('images/desktop-client.png')}" alt="Infinit Drive app">

Unfortunately, the notions of storage resource, network and volume are too technical for them to understand. Likewise, such users would probably like to receive a nice email explaining them how to set up and have things simplified for them.

This is why the notion of drive has been introduced. A drive is nothing more than an abstraction on top of a volume.

Rather than listing the networks and volumes a user is allowed to access in the graphical interface, only the drives the user has accepted to join will be shown. This way, should an administrator create a storage network with hundreds of volumes and a single one being abstracted as a drive, the end-user will only see this drive, making his/her experience as enjoyable as possible without limiting the possibilities of the underlying command-line tools.

### Create a drive ###

Creating a drive is as easy as any other operation. The following creates a drive named "workspace" based on the network "cluster" and volume "shared".

```
$> infinit-drive --create --as alice --network cluster --volume shared --name workspace --description "Alice’s, Bob’s and Charlie’s workspace" --push
Locally created drive "alice/workspace".
Remotely pushed drive "alice/workspace".
```

Note that the `--push` option is provided to publish it to the hub, making it retrievable by the other users, in particular the ones that we will be <a href="#invite-users">inviting</a> to join.

### List the drives ###

As for the other resources, one can very simply list the local drives. Note that refreshing the local snapshots can be achieved through the `--fetch` action:

```
$> infinit-drive --fetch --as alice
Fetched drives for user "alice".
$> infinit-drive --list
alice/workspace
```

### Invite users ###

It is now time to invite non-tech-savvy users to join the drive you’ve created for them.

Note that before you can reference a user, you need to fetch his/her public identity through the `infinit-user --fetch` command line. Likewise, every user to invite must have been issued a passport to connect to the network.

The sequence of commands below shows how to invite both Bob and Charlie. However it is assumed that Bob has already been issued a passport while Charlie is a completely new user, hence needs a passport.

```
$> infinit-user --fetch --as alice --name charlie
Fetched user "charlie".
$> infinit-passport --create --as alice --user charlie --network cluster --push
Locally created passport "alice/cluster: charlie".
Remotely pushed passport "alice/cluster: charlie".
$> infinit-drive --invite --as alice --name workspace --user bob --user charlie --push
Locally created invitations "alice/workspace".
Remotely pushed invitations "alice/workspace".
```

That’s it, Bob and Charlie have been invited to join the drive named "alice/workspace". Following the `--push` of the invitations, an email will be sent to notify them of such an invitation, letting them know how to proceed.

### Join a drive ###

Even though the drive abstraction has been introduced for users with the graphical interface, you can decide to join a drive through the command-line tools.

First, think about refreshing your local snapshots by fetching the drives you’ve been invited to and already have access to. You can then list the drives you have locally:

```
$> infinit-drive --fetch --as charlie
Fetched drive for user "charlie"
$> infinit-drive --list --as charlie
alice/workspace
```

Finally, you can join a drive through the following command:

```
$> infinit-drive --join --as charlie --name alice/workspace
Remotely pushed invitation "alice/workspace".
```

That’s it, you are now allowed to mount the volume (i.e 'alice/shared') associated with the drive to browse, store and access files. Note that you could have done that without going through the drive invitation process because you are using the command-line tools. Non-tech-savvy users however will appreciate having an interface clean of everything but the drives they have been invited to join.