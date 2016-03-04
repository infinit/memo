Upgrading a network
===================

This guide will help you upgrade an Infinit network to a newer version, providing users with new functionalities.

### Introduction ###

Every node in the network, being a client or a server, has the Infinit software installed in a specific version. Some may have downloaded the _0.4.0_ version from the Infinit website, others the software in _0.5.2_.

The [network](/documentation/technology#distributed-hash-table) (storage layer volumes rely upon to store blocks of data) is also configured to operate in a specific version. This version is defined in the network descriptor. Every node has a copy of this descriptor in its `$INFINIT_HOME` directory. To find out which version a network is operating in, one only needs to export the descriptor as shown below:

```
$> infinit-network --export --as alice --name cluster
{"consensus":{"replication-factor":1,"type":"paxos"},"name":"alice/cluster","overlay":{"accept_plain":false,"bootstrap_nodes":[],"contact_timeout_ms":120000,"encrypt":true, ...,"version":"0.3.0"}
```

One can notice that, in the example above, the `version` attribute indicates that the network's version is _0.3.0_.

What this means is that every node connecting to the network will not (necessarily) operate in its latest version, but will run in a compatibility mode matching the network's version. In other words, even a client in version _0.5.2_ will behave as if it were running the software in _0.3.0_.

Infinit has been designed to be backward compatible. As a result, every node (client or server) can always update its software to the very last version without any risk of impacting the whole system since it will always operate in the network's compatibility version.

This compatibility mode is required to make sure every node in the network speaks a common language. The common language is defined by the network's version number. This protection prevents a newer client, say in _0.4.6_, to use a new feature that would result in a block being stored that an older client, say in _0.3.0_, would not be able to understand.

_**NOTE**: The infinit-network and infinit-volume provide a `--compatibility-version` option that allows a user to connect a client to an infrastructure while operating in a specific version._

### Upgrading Process ###

The following provides advices that should be followed in order to make sure the process of upgrading goes as smoothly as possible:

1. Ask the users of the network to update their software (Infinit Drive or command-line tools) to the newest version available on [Infinit's website](http://infinit.sh/get-started)<!-- XXX[to change for /update] -->. Note that the updating method depends on your operating system (Linux, Windows, MacOS X etc.) and the way you retrieved the Infinit software (_apt-get_, _homebrew_, tarball etc.).
2. Once all the users have updated their software, say to version _0.5.2_, the administrator can proceed to upgrading the network. In the meantime, the users can keep using the network through the new client software as it will operate in the compatibility version defined in the network descriptor, as detailed earlier.
3. The administrator can upgrade the network descriptor to the version of his/her choice. Make sure not to exceed the highest version supported by all the clients or some will find themselves incapable to connect to the network. The following example shows how to upgrade a network descriptor:
   ```
   $> infinit-network --update --as alice --name cluster --compatibility-version 0.5
   Locally updated network "alice/cluster".
   ```
   At this point, the network descriptor specifies the new compatibility version clients must support to be able to connect (the common language). However, this new descriptor must be distributed to all the clients and servers for the new version to be taken into account.
4. Ask all the users to stop their client and server nodes being by gently killing the _infinit-volume_ process or quitting the Infinit Drive application.
5. As the administrator, you need to transmit the new network descriptor to all the nodes, clients and servers. This step depends on the mode of operation you chose. In decentralized (without the Hub), you will need to manually transfer the network descriptor to your users and other machines, through the _scp_ UNIX utility for instance. If you rely on the Infinit Hub, the process is much simpler as all you need is to update the Hub with the latest version of the network descriptor, as shown below. Note that before being able to push the new network descriptor, you must first remove it (`--pull`) from the Hub:
   ```
   $> infinit-network --pull --as alice --name cluster
   Remotely deleted network "alice/cluster".
   $> infinit-network --push --as alice --name cluster
   Remotely pushed network "alice/cluster".
   ```
6. Ask the network users to retrieve the updated network descriptor and re-launch the software, being through the command-line tools or the Infinit Drive's graphical user interface. When it comes to fetching the network descriptor, it once again depends on the mode of operation the administrator chose to use. In decentralized, every user needs to `--import` the network descriptor that was manually handed over to them in the previous step. Using the hub is as straightforward, as shown below an one of Bob's devices:
   ```
   $> infinit-network --fetch --as bob --name alice/cluster
   Fetched network "alice/cluster".
   ```

   One can very easily check that the local network descriptor's version has indeed changed:
   ```
   $> infinit-network --export --as bob --name alice/cluster
   {"consensus":{"replication-factor":1,"type":"paxos"},"name":"alice/cluster","overlay":{"accept_plain":false,"bootstrap_nodes":[],"contact_timeout_ms":120000,"encrypt":true, ...,"version":"0.5.0"}
   ```
7. Once the updated descriptor retrieved, every client and server can be re-launched. Every node will now benefit from the improvements brought by the new compatibility version, in this example all the functionalities from version _0.5.0_.

_**NOTE**: The subminor versions never bring additional features, or network protocol modifications. As such, upgrading a network to a subminor version is equivalent to upgrading to its minor version. In other words, upgrading to 0.5.2 is strictly equivalent to upgrading to 0.5.0._

Assuming a node with a lower version remains connected to the network after the upgrade, it would be in the incapacity to communicate with the other nodes that would refuse to talk in an outdated protocol. As a result, this node's volume would return I/O errors for every system call performed, leaving no choice to the user but to upgrade its local network descriptor and restart the software, as described in step _6_ and _7_.
