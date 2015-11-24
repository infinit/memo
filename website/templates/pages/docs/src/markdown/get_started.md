Get Started
=========

*The following is written for users of Mac OS X. If you are not using Mac OS X, please refer to the <a href="${route('doc_get_started')}?platform=linux">Linux</a> and <a href="${route('doc_get_started')}?platform=windows">Windows</a> guides.*

*Before proceeding, make sure you understand that this guide uses the Infinit command-line tools with a terminal window. You can otherwise download the <a href="${route('drive')}">Infinit desktop client</a>.*

*You don’t need to be a UNIX guru but you should be familiar with typing shell commands and interpreting outputs.*

1. Installation
-----------------

### Download and install Infinit’s dependencies

Infinit relies on [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to create filesystems in userland. If not already, you will need **install the OSXFUSE module** by following the instructions:

<a href="https://osxfuse.github.io/" class="button">Download OS X Fuse</a>

### Download and install the Infinit command-line tools

Please follow the link below to **download the Infinit command-line tools**:

<a href="#" class="button">Download</a>

Next, open your terminal and **extract the Infinit tarball**:

```
$> tar xjvf infinit-0.2.0.tbz
infinit-0.2.0/
infinit-0.2.0/bin/
infinit-0.2.0/lib
...
infinit-0.2.0/bin/infinit-storage
infinit-0.2.0/bin/infinit-user
infinit-0.2.0/bin/infinit-volume
[...]
$>
```

All the configuration files the Infinit command-line tools create and use are located in the `$INFINIT_HOME` directory which, by default, is set to `$HOME/.infinit/filesystem/`. You can edit your shell configuration to set `INFINIT_HOME` to another location if you would like.

Now that you’ve extracted the tarball, take a look. The extracted directory contains the following subdirectories:

* The `bin/` subdirectory contains the actual Infinit binaries such as infinit-user, infinit-network, etc.
* The `lib/` subdirectory contains all the libraries the above binaries depend upon to operate (excluding libosxfuse which should be in your `/usr/local/lib` directory as installed by OS X FUSE).
* The `test/` subdirectory is provided for you to quickly test those command-line tools, see below.

```
$> cd infinit-0.2.0/
$> ls
bin/    lib/    test/
$>
```

2. Basic Test
--------------

### Test by mounting an existing volume

For you to quickly (through a single command) try Infinit out, the following is going to let you **mount an existing volume** and access files through a mount point. Don’t worry, this volume only contains test files that we put there ourselves for you to test Infinit. You cannot do any harm to those files since you only have read-only access to this test volume.

Run the infinit-volume command by prefixing it with the environment variable `INFINIT_HOME=$PWD/../test/home` to tell the command where to look for the configuration files required for this test.

```
$> INFINIT_HOME=$PWD/test/home/ ./bin/infinit-volume --mount --as alice --name test --mountpoint $PWD/test/mountpoint/
… XXX ...
$>
```


This command mounts the volume named ‘test’ on behalf of the user ‘alice’ and makes it accessible through the mount point `test/mountpoint/`.

That’s it, you can now access the files in the ‘test’ volume by browsing the mount point as with any other POSIX-compliant file system.

```
$> ls test/mountpoint/
… XXX …
$>
```

Noteworthy is that volume “test” contains several gigabytes of data. However, unlike cloud storage services like Dropbox, you were able to browse the volume’s content without having to wait hours for all the files to be downloaded locally. This is because only the data actually accessed is retrieved on demand.

You can stop this test by hitting CTRL^C or killing the infinit-volume process.

### Set up your own storage infrastructure

It is now time for you to **create your own storage infrastructure**. The following will guide you step by step to set up an infrastructure involving a single user and two computing devices (named A and B) that will both contribute storage capacity to store the blocks of data composing the files.

<img src="" alt="two devices with a file composed of blocks that are distributed among those devices">

_**NOTE**: For more information to learn how to set up a completely decentralized infrastructure i.e without creating an account on the Hub, how to plug cloud storage services such as AWS S3, Google Cloud Storage or even Dropbox or how to invite non-tech-savvy users to join and use storage infrastructure to store and access files, please refer to the <a href="${route('reference')}">reference documentation</a>._

First, add the `bin/` directory to the PATH environment variable to be able to invoke the command-line tools from anywhere:

```
$> export PATH=$PWD/bin/:$PATH
$>
```

3. Create a user
----------------------

The first step consists of creating a user on the hub. All the commands that follow take as example the user name ‘bob’ but you are welcome to pick your own unique one:

```
$[device A]> infinit-user --signup --name bob --email bob@company.com --fullname “Bob”
Generating RSA keypair.
Generated user "bob".
… XXX ...
$[device A]>
```

4. Create a storage resource
--------------------------------------

A storage resource behaves like a hard disk, storing data blocks without understanding of its meaning. The beauty of Infinit is that it is completely agnostic of the nature of such storage resources.

Next, we are going to declare a local storage resource. A local storage stores data blocks as files in a directory (such as `/var/storage/infinit/`) on the local file system.

The binary infinit-storage is used for this purpose. The option `--filesystem` is used to indicate that the storage will be local while the --path option specifies which folder to put the blocks of data:

```
$[device A]> infinit-storage --create --filesystem --path /var/storage/infinit/ --name local --capacity 1GB
Created storage "local".
$[device A]>
```

5. Create a network
--------------------------

Now that we have at least one storage resource to store data, we can create a network interconnecting different machines.

The infinit-network command is used to create the network, specifying a name for the network and the list of storage resources to rely upon. In this example, only the ‘local’ storage resource is used but you could plug as many as you like:

```
$[device A]> infinit-network --create --as bob --storage local --kelips --k 1 --name mine --push
Created network "mine".
$[device A]>
```

_**NOTE**: The `--push` option is used to publish the created network (likewise for volumes) onto the hub for it to be easily fetched on another device or shared with another user._

6. Create a volume
-------------------------

The last step on this device consists of creating a logical volume to store and access files. Volumes can be manipulated through the infinit-volume binary as shown next:

```
$[device A]> infinit-volume --create --as bob --network mine --name personal --push
Starting network ".../mine".
Creating volume root blocks.
Created volume ".../personal".
$[device A]>
```
That’s it, you’ve created a ‘personal’ volume i.e. a file system. The blocks that the files are composed of are stored through the ‘personal’ volume and will be distributed across the network named ‘mine’, currently composed of a single computer.

7. Mount the volume
--------------------------

Let’s access this volume by mounting it as simply as any other file system:

```
$[device A]> mkdir mnt/
$[device A]> infinit-volume --mount --as bob --name personal --mountpoint mnt/  --async-writes --cache --publish
Running network ".../mine".
Running volume ".../personal".
[…]
```

That’s it! You can now create, list and access files from the mount point `mnt/`. Try creating a file right now:

```
$[device A]> echo “everything is” > mnt/awesome.txt
$[device A]> ls mnt/
awesome.txt
$[device A]>
```

_**NOTE**: This command does not return. You can make it run in the background if you prefer. To stop it and unmount the volume, just hit `CTRL^C`._

8. Access from another machine
-------------------------------------------

Now that you have successfully mounted the volume on your machine, it could be interesting to access the same data on another device.

XXX

For that, we need to transmit the user identity onto the other device. Unlike the networks and volumes, the user’s key pair cannot be stored on the hub because of its critical nature. Indeed, you do not want to put it on the cloud but rather to move it manually and securely as you would do with your SSH key.

Let’s first export the user’s identity and store it into a file.

```
$[device A]> infinit-user --export --full --name bob > bob.user
WARNING: you are exporting the user "bob" including the private key
WARNING: anyone in possession of this information can impersonate that user
WARNING: if you mean to export your user for someone else, remove the --full flag
$[device A]>
```

_**NOTE**: The --full option means that both the public and private parts of the user’s key pair will be exported, hence the warning._

From that point, you are free to move the file bob.user through the method of your choice such as SCP or the [Infinit file transfer application](https://infinit.io) ;).

Now let’s move to device B. First, you need to download, install and configure the Infinit command-line tools on this new device as you did for device A. Please refer to the first sections of this guide for that purpose.

Once installed, let’s import the `bob.user` file to re-create the user on this device.

```
$[device B]> infinit-user --import --input bob.user
Imported user “bob”.
$[device B]>
```

The next steps consist in fetching the resources that you previously pushed on the hub: networks and volumes.

```
$[device B]> infinit-network --fetch --as bob --name mine
Fetched network ".../mine".
$[device B]> infinit-volume --fetch --as bob --name personal
Fetched volume ".../personal".
$[device B]>
```

Let’s connect this device to the ‘mine’ network you created on the device A, contributing the storage you just created.

```
$[device B]> infinit-network --join --as bob --name mine --storage local
Joined network ".../mine".
$[device B]>
```

Finally, the volume can be mounted on device B as show below:

```
$[device B]> mkdir mnt/
$[device B]> infinit-volume --mount mnt/ --as bob --name personal --async-writes --publish
Running network ".../mine".
Running volume ".../personal".
[…]
```

It is now time to check if the file you created on device A is synchronized with device B:

```
$[device B]> ls mnt/
awesome.txt
$[device B]> cat mnt/awesome.txt
everything is
$[device B]>
```

That’s it, you’ve created a file system that you quickly connected on two machines, without having to go through a complicated administration process.

You can now invite other users to join the network to contribute additional storage and/or share files and collaborate. Just keep in mind that the storage infrastructure presented in this guide is very sensitive to computers disconnecting, possibly rendering files inaccessible. Please take a look to the <a href="${route('reference')}">reference documentation</a> to learn how to plus additional storage, add more nodes, invite friends and configure the network to be more resilient to faults.

<a href="https://twitter.com/infinit_sh" class="icon-twitter">Follow us on Twitter</a>

XXX
