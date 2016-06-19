Best Practices
==============

This document proposes best practices when administrating an Infinit storage infrastructure.

Backup
------

In order to ensure that you never lose access to your Infinit storage infrastructure, it is important to make backups of descriptors and users. The requirements for what you should backup will depend on if you use the Hub or not.

### User ###

*Always backup.*

As Infinit relies on public/private key encryption, it is vital to ensure that you do not lose your user's key pair. Loss of the key pair will mean that you will lose access to and the ability to administrate networks. Even if you use the Hub, the default push action does not include the private key. To backup a user's key pair, you can export it in full to a file as follows:

```
$> infinit-user --export --full --name alice --output alice.user
WARNING: you are exporting the user "alice" including the private key
WARNING: anyone in possession of this information can impersonate that user
WARNING: if you mean to export your user for someone else, remove the --full flag
Exported user "alice".
```

To restore a backed up user, you use the `--import` mode.

```
$> infinit-user --import --input alice.user
Imported user "alice".
```

### Storage ###

*Always backup.*

A storage is a device specific concept that is not pushed to the Hub. There are two components to a storage: the storage descriptor and the block storage itself. If you are using a *filesystem* storage without replication, you will need to backup the block store which is the folder you specified with `--path` on creation.

To backup the storage descriptor, you use the `--export` mode.

```
$> infinit-storage --export local --output local.storage
Exported storage "local".
```

The storage descriptor can restored using the `--import` mode.

```
$> infinit-storage --import local --input local.storage
Imported storage "local".
```

### Network ###

*Only backup if not using the Hub.*

A network descriptor is backed up using the `--export` mode.

```
$> infinit-network --export --as alice --name cluster --output cluster.network
Exported network "alice/cluster".
```

To restore the network, you will need to import it and then relink the network with the storage that it was using before.

```
$> infinit-network --import --input cluster.network
Imported network "alice/cluster".
$> infinit-network --link --as alice --name cluster --storage local
Linked device to network "alice/cluster".
```

### Volume ###

*Only backup if not using the Hub.*

A volume is backed up by exporting it to a file as was done for the other objects.

```
$> infinit-volume --export --as alice --name shared --output shared.volume
Exported volume "alice/shared".
```

It can then be restored using the `--import` mode.

```
$> infinit-volume --import --input shared.volume
Imported volume "alice/shared".
```

### Drive ###

As drives require the Hub to operate, you should not need to back these up. You can, however, back them up using the same technique as for a volume if you wish.

Untrusted storage nodes
-----------------------

In the case that the storage nodes cannot be trusted, it is important that the data stored by them is encrypted in such a way that the storage nodes cannot read it (i.e.: the data is *encrypted at rest*).

As access control for the network and the volume is user based, it's important to use a different user for the storage nodes and the users who will using the volume.

Making a volume world readable or writable will allow anyone who has a passport for the network to access the data in the volume; including any storage user. If you would like similar functionality, you can create an ACL group containing all the volume users and then set the permissions for the group recursively with the `--enable-inherit` option on the root of the volume.

_**Note**: Storage nodes must be able to write to the network in order to perform rebalancing. As such, do not use the `--deny-write` flag when creating a passport for them if you wish to use rebalancing._

