Get Started
=========

<div>
% if os() == "Windows":
<blockquote class="warning">
<strong>Notice:</strong> The following is written for users of Mac OS X and Linux only. Please <a href="http://infinit.us2.list-manage.com/subscribe?u=29987e577ffc85d3a773ae9f0&id=b537e019ee" target="_blank">sign up to our newsletter</a> to know when it’s available for <strong>Windows</strong>.
</blockquote>

% elif "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):
<p><em>The following is written for users of Mac OS X. If you are not using one of these platforms, please refer to the <a href="${route('doc_get_started_linux')}">Linux</a> or <a href="${route('doc_get_started_windows')}">Windows</a> guide.</em></p>

% else:
<p><em>The following is written for users of Linux. If you are not using one of these platforms, please refer to the <a href="${route('doc_get_started_mac')}">Mac</a> or <a href="${route('doc_get_started_windows')}">Windows</a> guide.</em></p>

% endif
</div>

*Before proceeding, this guide uses the Infinit command-line tools with a terminal window. You don’t need to be a UNIX guru but you should be familiar with typing shell commands and interpreting outputs. You can otherwise download the <a href="${route('drive')}">Infinit Drive desktop client</a>.*

1. Installation
-----------------

### Download and install Infinit’s dependencies

<img class="fuse" src="${url('images/icons/osxfuse.png')}" alt="FUSE">

<div>
% if "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):
<p>Infinit relies on [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to create filesystems in userland. You will need to install the OSXFUSE from the link below:</p>

<p><a href="https://github.com/osxfuse/osxfuse/releases/download/osxfuse-3.0.9/osxfuse-3.0.9.dmg" class="button">Download OSXFUSE</a></p>

_**NOTE**: Infinit requires a version of OSXFUSE that is newer than available on https://osxfuse.github.io/._

% else:

<p>Infinit relies on [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to create filesystems in userland. You will need to install FUSE using your distribution's package manager. For example, if you use a Debian based distribution, you would use `apt-get` :</p>

% endif
</div>

### Download and install the Infinit command-line tools

<div>
% if "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):
<img class="infinitcli" src="${url('images/icons/infinit-cli.png')}" alt="Infinit Command Line Tools">
<p>Click the link below to download the Infinit command-line tools:</p>

<a href="https://storage.googleapis.com/sh_infinit_releases/osx/Infinit-x86_64-osx-clang3-${tarball_version}.tbz
" class="button">Download Command Line Tools</a>

% else:
<p>If you are using Ubuntu 14.04 or later, you can use our repository to install the command-line tools. Otherwise, skip to the <a href='#linux-tarball-install'>Tarball Install</a>.</p>

<h3 id="linux-ubuntu-install">&#9679; Ubuntu install</h3>
<p>First import the public key used by the pacakge management system:</p>
<pre><code>$> sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 3D2C3B0B
Executing: gpg --ignore-time-conflict --no-options --no-default-keyring --homedir /tmp/tmp.fCTpAPiWSi --no-auto-check-trustdb --trust-model always --keyring /etc/apt/trusted.gpg --primary-keyring /etc/apt/trusted.gpg --keyserver keyserver.ubuntu.com --recv-keys 3D2C3B0B
gpg: requesting key 3D2C3B0B from hkp server keyserver.ubuntu.com
gpg: key 6821EB43: public key "Infinit <contact@infinit.one>" imported
gpg: Total number processed: 1
gpg:               imported: 1  (RSA: 1)
</code></pre>

<p>Then add the repository locally:</p>
<pre><code>$> sudo add-apt-repository "deb https://debian.infinit.sh/ trusty main"
</code></pre>

<p>Finally, you can update your local list of packages and install the command-line tools as you would any other package:</p>

<pre><code>$> sudo apt-get update
...
Reading package lists... Done
$> sudo apt-get install infinit
Reading package lists... Done
Building dependency tree
Reading state information... Done
The following NEW packages will be installed:
  infinit
0 upgraded, 1 newly installed, 0 to remove and 24 not upgraded.
Need to get 51,6 MB/51,6 MB of archives.
After this operation, 51,6 MB of additional disk space will be used.
Selecting previously unselected package infinit.
(Reading database ... 208105 files and directories currently installed.)
Preparing to unpack .../infinit_${tarball_version}_amd64.deb ...
Unpacking infinit (${tarball_version}) ...
Setting up infinit (${tarball_version}) ...
</code></pre>
<p></p>
<p>You can now change to the install directory and <a href='#2-basic-test'>test</a> your install.</p>
<pre><code>$> cd /opt/infinit
</code></pre>

<h3 id="linux-tarball-install">&#9679; Tarball Install</h3>

<img class="infinitcli" src="${url('images/icons/infinit-cli.png')}" alt="Infinit Command Line Tools">
<p>Click the link below to download the Infinit command-line tools:</p>

<a href="https://storage.googleapis.com/sh_infinit_releases/linux64/Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}.tbz
" class="button">Download Command Line Tools Tarball</a>

% endif
</div>

<br>

Next, open your terminal and extract the Infinit tarball:

```

% if "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):
$> tar xjvf Infinit-x86_64-osx-clang3-${tarball_version}.tbz
% else:
$> tar xjvf Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}.tbz
% endif
Infinit-${tarball_version}/
Infinit-${tarball_version}/bin/
Infinit-${tarball_version}/lib
...
Infinit-${tarball_version}/bin/infinit-storage
Infinit-${tarball_version}/bin/infinit-user
Infinit-${tarball_version}/bin/infinit-volume
...
```

All the configuration files that the Infinit command-line tools create and use are located in the `$INFINIT_DATA_HOME` directory which, by default, is set to `$HOME/.local/share/infinit/filesystem/`, following the [XDG Base Directory Specification](http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html). You can edit your shell configuration to set `INFINIT_DATA_HOME` to another location if you would like.

Now that you’ve extracted the tarball, take a look. The extracted directory contains the following subdirectories:

* The `bin/` subdirectory contains the actual Infinit binaries such as _infinit-user_, _infinit-network_, etc.
* The `lib/` subdirectory contains all the libraries the above binaries depend upon to operate (excluding the FUSE library you installed earlier).
* The `share/infinit/filesystem/test/` subdirectory is provided for you to quickly test the command-line tools, see below.

```
$> cd Infinit-${tarball_version}/
$> ls
bin/    lib/    share/
```

2. Basic Test
--------------

For you to quickly (through a single command) try Infinit out, the following is going to let you **mount an existing volume** and access files through a mount point. Don’t worry, this file system only contains test files that we put there ourselves for you to test Infinit. You cannot do any harm to those files since you only have read-only access to this test volume.

Run the _infinit-volume_ command by prefixing it with the environment variable `INFINIT_DATA_HOME=$PWD/share/infinit/filesystem/test/home` to tell the command where to look for the configuration files required for this test.

```
$> INFINIT_DATA_HOME=$PWD/share/infinit/filesystem/test/home/ ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
```

_**NOTE**: You can stop this test volume by hitting `CTRL^C` or by interrupting the infinit-volume process._

This command mounts the volume named ‘infinit/demo’ on behalf of the user ‘demo’ and makes it accessible through the mount point `~/mnt-demo/`.

Now open another terminal. You can access the files in the ‘demo’ volume by browsing the mount point `~/mnt-demo/` as you would any other POSIX-compliant filesystem.

Infinit streams the data as it is needed from our server in the US. This allows you to access the files immediately without having to wait for them to be cloned locally as you would with a cloud storage service like Dropbox.

```
$> ls ~/mnt-demo/
Aosta Valley/         Brannenburg/          Cape Town/            Infinit_MakingOf.mp4 New York/             Paris/
```

3. Create Infrastructure
-------------------------------

It is now time for you to **create your own storage infrastructure**. What follows is a step-by-step guide to set up infrastructure for a single user with two devices (named A and B). One of the devices will contribute storage capacity to store the blocks of data composing the files while the second device will read and write blocks over the network. This creates an NFS-like system even though it is also possible to create infrastructure where more than one node stores blocks.

<img src="${url('images/schema-two-clients.png')}" alt="two devices with a file composed of blocks that are distributed among those devices">

*__NOTE__: For more information to learn how to set up completely decentralized infrastructure, how to plug cloud storage services such as AWS S3, Google Cloud Storage etc. or how to invite non-tech-savvy users to join and use storage infrastructure to store and access files as usual, please refer to the <a href="${route('doc_reference')}">reference documentation</a>.*

First, add the `bin/` directory to the PATH environment variable to be able to invoke the command-line tools from anywhere:

```
$> export PATH=$PWD/bin/:$PATH
```

### Create a user

The first step consists of creating a user on the Hub. All the commands that follow use the user name ‘bob’ but you should **pick your own unique user name**:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-user --signup --name bob --email bob@company.com --fullname "Bob"
Generating RSA keypair.
Remotely pushed user "bob".
</code></pre>

### Create a storage resource

A storage resource behaves like a hard disk, storing data blocks without understanding the data’s meaning. The beauty of Infinit is that it is completely agnostic of the nature of such storage resources.

Next, we are going to declare a local storage resource. A local storage stores data blocks as files in a directory (such as `/var/storage/infinit/`) on the local filesystem.

The binary _infinit-storage_ is used for this purpose. The option `--filesystem` is used to indicate that the storage will be on the local filesystem:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-storage --create --filesystem --name local --capacity 1GB
Created storage "local".
</code></pre>

### Create a network

Now that we have at least one storage resource to store data, we can create a network interconnecting different machines.

The _infinit-network_ command is used to create the network, specifying a name along with the list of storage resources to rely upon. We will ignore the other options now but you can read about them in the <a href="${route('doc_reference')}">reference documentation</a>. In this example, only the ‘local’ storage resource is used but you could plug as many as you like. Obviously, you need to substitute 'bob' with your username:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-network --create --as bob --storage local --kelips --name my-network --push
Locally created network "bob/my-network".
Remotely pushed network "bob/my-network".
</code></pre>

*__NOTE__: The `--push` option is used to publish the created network (likewise for other objects) onto the Hub for it to be easily fetched on another device or shared with other users.*

### Create a volume

The last step on this device consists of creating a logical volume to store and access files. Volumes can be manipulated through the _infinit-volume_ binary as shown next:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-volume --create --as bob --network my-network --name my-volume --push
Locally created volume "bob/my-volume".
Remotely pushed volume "bob/my-volume".
</code></pre>

That’s it, you’ve created a volume named ‘my-volume’ i.e. a filesystem. The blocks that the files are composed of will be distributed across the network named ‘my-network’, currently composed of a single computer with a single storage resource.

### Mount the volume

Let’s access this volume by mounting it as easily as any other filesystem:

<pre>
<div><span>Device A</span></div>
<code>$> infinit-volume --mount --as bob --name my-volume --mountpoint mnt/ --async --cache --publish
Fetched endpoints for "bob/my-network".
Running network "bob/my-network".
Remotely pushed endpoints for "bob/my-network".
Running volume "bob/my-volume".
</code></pre>

_**NOTE**: This command does not return. You can make it run in the background if you prefer. To stop it and unmount the volume, just hit `CTRL^C` or interrupt the process. You should wait until the end of the guide to stop this process though._

That’s it! You can now create, list and access files from the mount point `mnt/`. Try creating a file right now:

<pre>
<div><span>Device A</span></div>
<code>$> echo "everything is" > mnt/awesome.txt
$> cat mnt/awesome.txt
everything is
</code></pre>

### Access from another machine

Now that you have successfully created and mounted a volume on your machine, it would be interesting to access the data from your other devices. If you don’t have another device, you can simulate another device by opening another terminal and setting `INFINIT_DATA_HOME` to a different directory.

In order to access your volume from another device, you will need to transfer your user’s identity to that device. A user’s identity is analogous to an SSH key pair and should be treated in the same way. Because of its critical nature, it is not stored on the Hub like network and volume descriptors could be for instance. For this guide, we will rely on the Hub to transmit the identity in encrypted form to the other device.

Let’s transmit the user’s identity to the other device. Note that it will be encrypted with a passphrase chosen by you. Also you will only have 5 minutes to retrieve it on device B.

<pre>
<div><span>Device A</span></div>
<code>$> infinit-device --transmit --user --as bob
Passphrase: ********
Transmitted user identity for "bob".
User identity on the Hub for: 297 seconds
</code></pre>

Now let’s move to device B. First, you need to download, install and configure the Infinit command-line tools on this new device as you did for device A. Please refer to the first sections of this guide for that purpose.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-device --receive --user --name bob
Passphrase: ********
Received user identity for "bob".
</code></pre>

_**NOTE**: The pairing process may have expired on device A. If so, please try again and enter the passphrase on device B in order to retrieve your user identity before the counter runs down._

The next steps consist of fetching the resources that you previously pushed on the Hub: networks and volumes.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-network --fetch --as bob --name my-network
Fetched network "bob/my-network".
$> infinit-volume --fetch --as bob --name my-volume
Fetched volume "bob/my-volume".
</code></pre>

Let’s link this device to the ‘my-network’ network you created on device A.

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-network --link --as bob --name my-network
Linked device to network "bob/my-network".
</code></pre>

Finally, the volume can be mounted on device B as simply as on device A:

<pre class="alternate">
<div><span>Device B</span></div>
<code>$> infinit-volume --mount --mountpoint mnt/ --as bob --name my-volume --async --cache --publish
Fetch endpoints for "bob/my-network".
Running network “bob/my-network”.
Running volume "bob/my-volume".
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

You can now invite other users to join the network, to contribute additional storage and/or share files and collaborate. Just keep in mind that the storage infrastructure presented in this guide is very sensitive to computers disconnecting, possibly rendering files inaccessible. Please take a look at the <a href="${route('doc_reference')}">reference documentation</a> to learn how to add additional storage, add more nodes, invite friends and configure the network to be more resilient.

<a href="https://www.facebook.com/groups/1518536058464674/" class="icon-facebook">Join our Facebook Group</a>

<a href="#slack" class="icon-slack">Ask us something on Slack</a>

<a href="https://twitter.com/infinit_sh" class="icon-twitter">Follow us on Twitter</a>
