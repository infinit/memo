Get Started
=========

<div>
% if os() == "Windows":
<blockquote class="warning">
<strong>Notice:</strong> The following is written for users of Mac OS X and Linux only. Please <a href="http://infinit.us2.list-manage.com/subscribe?u=29987e577ffc85d3a773ae9f0&id=b537e019ee" target="_blank">sign up to our newsletter</a> to know when it will be available on <strong>Windows</strong>.
</blockquote>

% else:
<p><em>The following is written for users of Mac OS X and Linux. If you are not using it, please refer to the <a href="${route('doc_get_started_windows')}">Windows</a> guide.</em></p>
% endif
</div>

*Before proceeding, make sure you understand that this guide uses the Infinit command-line tools with a terminal window. You can otherwise download the <a href="${route('drive')}">Infinit Drive desktop client</a>.*

*You don’t need to be a UNIX guru but you should be familiar with typing shell commands and interpreting outputs.*

1. Installation
-----------------

### Download and install Infinit’s dependencies

<img class="fuse" src="${url('images/icons/osxfuse.png')}" alt="FUSE">

<div>
% if os() == "Macintosh":
<p>Infinit relies on [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to create filesystems in userland. If not already, you will need install the OSXFUSE core module by following the instructions:</p>

<p><a href="https://storage.googleapis.com/sh_infinit_releases/osx/osxfuse-core.dmg
" class="button">Download OSXFUSE core</a></p>

_**NOTE**: Infinit requires a version of OSXFUSE core that is newer than available on https://osxfuse.github.io/._
% else:
<p>Infinit relies on [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to create filesystems in userland. You will need to install FUSE using your distribution's package manager. For example, if you use a Debian based distribution, you would use *apt-get* :</p>

```
$ sudo apt-get install fuse
```
% endif
</div>

### Download and install the Infinit command-line tools

<img class="infinitcli" src="${url('images/icons/infinit-cli.png')}" alt="Infinit Command Line Tools">

Please follow the link below to download the Infinit command-line tools:

<div>
% if os() == "Macintosh":
<a href="https://storage.googleapis.com/sh_infinit_releases/osx/infinit-cli.tbz
" class="button">Download Command Line Tools</a>
% else:
<a href="https://storage.googleapis.com/sh_infinit_releases/linux64/infinit-cli.tar.tbz
" class="button">Download Command Line Tools</a>
% endif
</div>

<br>

Next, open your terminal and extract the Infinit tarball:

```
$ tar xjvf infinit-0.2.0.tbz
infinit-0.2.0/
infinit-0.2.0/bin/
infinit-0.2.0/lib
...
infinit-0.2.0/bin/infinit-storage
infinit-0.2.0/bin/infinit-user
infinit-0.2.0/bin/infinit-volume
...
```

All the configuration files that the Infinit command-line tools create and use are located in the `$INFINIT_HOME` directory which, by default, is set to `$HOME/.infinit/filesystem/`. You can edit your shell configuration to set `INFINIT_HOME` to another location if you would like.

Now that you’ve extracted the tarball, take a look. The extracted directory contains the following subdirectories:

* The `bin/` subdirectory contains the actual Infinit binaries such as `infinit-user`, `infinit-network`, etc.
* The `lib/` subdirectory contains all the libraries the above binaries depend upon to operate (excluding libosxfuse which should be in your `/usr/local/lib` directory as installed by FUSE for OS X).
* The `test/` subdirectory is provided for you to quickly test those command-line tools, see below.

```
$ cd infinit-0.2.0/
$ ls
bin/    lib/    test/
```

2. Basic Test
--------------

For you to quickly (through a single command) try Infinit out, the following is going to let you **mount an existing volume** and access files through a mount point. Don’t worry, this volume only contains test files that we put there ourselves for you to test Infinit. You cannot do any harm to those files since you only have read-only access to this test volume.

Run the `infinit-volume` command by prefixing it with the environment variable `INFINIT_HOME=$PWD/test/home` to tell the command where to look for the configuration files required for this test.

```
$ INFINIT_HOME=$PWD/test/home/ ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint $PWD/test/mountpoint/ --fetch-endpoints --cache
```

This command mounts the volume named ‘infinit/demo’ on behalf of the user ‘demo’ and makes it accessible through the mount point `test/mountpoint/`.

That’s it, you can now access the files in the ‘demo’ by browsing the mount point as you would any other POSIX-compliant filesystem.

```
$ ls test/mountpoint/
Aosta Valley
Brannenburg
Cape Town
Infinit_MakingOf.mp4
New York
Paris
```

Noteworthy is that the volume contains several hundred megabytes of data. However, unlike cloud storage services like Dropbox, you were able to browse the volume’s content without having to for all the files to be downloaded locally. This is because only the data actually accessed is retrieved on demand.

You can stop this test by hitting `CTRL^C` or by interrupting the `infinit-volume` process.

3. Create Infrastructure
--------------------------------

It is now time for you to **create your own storage infrastructure**. What follows is a step-by-step guide to set up infrastructure for a single user with two devices (named A and B). One of the devices will contribute storage capacity to store the blocks of data composing the files while the second device will read and write blocks over the network. This creates an NFS like system. It is also possible to create infrastructure where more than one node stores blocks.

<img src="${url('images/schema-two-clients.png')}" alt="two devices with a file composed of blocks that are distributed among those devices">

_**NOTE**: For more information to learn how to set up completely decentralized infrastructure i.e without creating an account on the Hub, how to plug cloud storage services such as AWS S3, Google Cloud Storage or even Dropbox or how to invite non-tech-savvy users to join and use storage infrastructure to store and access files, please refer to the <a href="${route('doc_reference')}">reference documentation</a>._

First, add the `bin/` directory to the PATH environment variable to be able to invoke the command-line tools from anywhere:

```
$ export PATH=$PWD/bin/:$PATH
```

### Create a user

The first step consists of creating a user on the Hub. All the commands that follow use the user name ‘bob’ but you should pick your own unique one:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-user --signup --name bob --email bob@company.com --fullname "Bob"
Generating RSA keypair.
Remotely pushed user "bob".
</code></pre>

### Create a storage resource

A storage resource behaves like a hard disk, storing data blocks without understanding the data’s meaning. The beauty of Infinit is that it is completely agnostic of the nature of such storage resources.

Next, we are going to declare a local storage resource. A local storage stores data blocks as files in a directory (such as `/var/storage/infinit/`) on the local filesystem.

The binary `infinit-storage` is used for this purpose. The option `--filesystem` is used to indicate that the storage will be on the local filesystem:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-storage --create --filesystem --name local --capacity 1GB
Created storage "local".
</code></pre>

### Create a network

Now that we have at least one storage resource to store data, we can create a network interconnecting different machines.

The `infinit-network` command is used to create the network, specifying a name for the network and the list of storage resources to rely upon. In this example, only the ‘local’ storage resource is used but you could plug as many as you like:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-network --create --as bob --storage local --kelips --k 1 --name mine --push
Locally created network "bob/mine".
Remotely pushed network "bob/mine".
</code></pre>

*__NOTE__: The `--push` option is used to publish the created network (likewise for volumes) onto the Hub for it to be easily fetched on another device or shared with other users.*

### Create a volume

The last step on this device consists of creating a logical volume to store and access files. Volumes can be manipulated through the `infinit-volume` binary as shown next:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-volume --create --as bob --network mine --name personal --push
Locally created volume “bob/personal”.
Remotely pushed volume “bob/personal”.
$>
</code></pre>

That’s it, you’ve created a ‘personal’ volume i.e. a filesystem. The blocks that the files are composed of are stored through the ‘personal’ volume and will be distributed across the network named ‘mine’, currently composed of a single computer.

### Mount the volume

Let’s access this volume by mounting it as simply as any other filesystem:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-volume --mount --as bob --name personal --mountpoint mnt/  --async --cache &#8211;&#8211;publish
Fetched endpoints for "bob/mine".
Running network "bob/mine".
</code></pre>
```

That’s it! You can now create, list and access files from the mount point `mnt/`. Try creating a file right now:

<pre>
<div><span>Device A</span></div>
<code>$> echo "everything is" > mnt/awesome.txt
$> cat mnt/awesome.txt
everything is
</code></pre>

_**NOTE**: This command does not return. You can make it run in the background if you prefer. To stop it and unmount the volume, just hit `CTRL^C` or interrupt the process. You should wait until the end of the guide to stop this process though._

### Access from another machine

Now that you have successfully created and mounted a volume on your machine, it would be interesting to access the data from your other devices. In this guide we’ll show you how to connect your other devices. In the case you don’t have another device, you can can skip to the end of the guide.

In order to access your volume from another device, you will need to transfer your user’s identity to the device. A user’s identity is analogous to an SSH key pair and should be treated in the same way. Because of its critical nature, it is not stored on the Hub like networks and volumes are. For this guide, we will rely on the Hub to transmit the identity in encrypted form to the other device.

Let’s first transmit the user’s identity to the Hub. Note that the identity will be encrypted with a one time passphrase chosen by you and will only be present on the Hub for a fixed period of time.

<pre>
<div><span>Device A</span></div>
<code>$> infinit-device --transmit --user --as bob
Password: ********
Transmitted user identity for "bob".
</code></pre>

Now let’s move to device B. First, you need to download, install and configure the Infinit command-line tools on this new device as you did for device A. Please refer to the first sections of this guide for that purpose.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-device --receive --user --name bob
Received user identity for "bob".
</code></pre>

_**NOTE**: The pairing process may have expired on device A. If so, please try again and enter the passphrase on device B in order to retrieve your user identity B before the counter runs down._

The next steps consist of fetching the resources that you previously pushed on the Hub: networks and volumes.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-network --fetch --as bob --name mine
Fetched network "bob/mine".
$> infinit-volume --fetch --as bob --name personal
Fetched volume "bob/personal".
</code></pre>

Let’s connect this device to the ‘mine’ network you created on device A.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-network --join --as bob --name bob/mine
Joined network "bob/mine".
</code></pre>

Finally, the volume can be mounted on device B as simply as on device A:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-volume --mount --mountpoint mnt/ --as bob --name personal --async --cache --publish
Fetch endpoints for "bob/mine".
Running network “bob/mine”.
Running volume "bob/personal".
</code></pre>

It is now time to check if the file you created on device A is synchronized with device B:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> ls mnt/
awesome.txt
$> cat mnt/awesome.txt
everything is
</code></pre>

That’s it, you’ve created a filesystem that you quickly connected to with two devices, without having to go through a complicated administrative process.

4. Go Further
------------------

You can now invite other users to join the network, to contribute additional storage and/or share files and collaborate. Just keep in mind that the storage infrastructure presented in this guide is very sensitive to computers disconnecting, possibly rendering files inaccessible. Please take a look to the <a href="${route('doc_reference')}">reference documentation</a> to learn how to add additional storage, add more nodes, invite friends and configure the network to be more resilient to faults.

<a href="https://www.facebook.com/groups/1518536058464674/" class="icon-facebook">Join our Facebook Group</a>

<a href="#slack" class="icon-slack">Ask us something on Slack</a>

<a href="https://twitter.com/infinit_sh" class="icon-twitter">Follow us on Twitter</a>
