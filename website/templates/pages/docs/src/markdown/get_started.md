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

<h3 class="skip">Download and install Infinit’s dependencies</h3>

<div>
% if "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):

<%include file='get_started/mac_fuse_install.html'/>

% else:

<%include file='get_started/linux_fuse_install.html'/>

% endif
</div>

<h3 class="skip">Download and install the Infinit command-line tools</h3>

<div>
% if "mac" in request.path or (os() == "Macintosh" and "linux" not in request.path):

<p>If you have <a href="http://brew.sh">homebrew</a>, you can use our repository to install the command-line tools (recommended). Otherwise, choose the Tarball install.</p>

<ul data-tabs class="tabs">
  <li><a data-tab href="#mac-homebrew-install" class="active">• Homebrew Install</a></li>
  <li><a data-tab href="#mac-tarball-install">• Tarball Install</a></li>
</ul>

</div> <!-- need closing condition div before opening a new one -->

<div data-tabs-content class="data-tabs-content">
  <div data-tabs-pane class="tabs-pane active" id="mac-homebrew-install">
    <h3 class="skip">Homebrew Install</h3>
    <%include file='get_started/mac_homebrew_install.html'/>
  </div>

  <div data-tabs-pane class="tabs-pane" id="mac-tarball-install">
    <h3 class="skip">Tarball Install</h3>
    <%include file='get_started/mac_tarball_install.html'/>

    <p>Next, open your terminal and extract the Infinit tarball:</p>

    <pre><code>$> tar xjvf Infinit-x86_64-osx-clang3-${tarball_version}.tbz
Infinit-${tarball_version}/
Infinit-${tarball_version}/bin/
Infinit-${tarball_version}/lib
...
Infinit-${tarball_version}/bin/infinit-storage
Infinit-${tarball_version}/bin/infinit-user
Infinit-${tarball_version}/bin/infinit-volume
...</code></pre>

    <p>Now that you’ve extracted the tarball, take a look. The extracted directory contains the following subdirectories:</p>

    <pre><code>$> cd Infinit-${tarball_version}/
$> ls
bin/    lib/    share/
</code></pre>

  </div>
</div>

<div> <!-- open it again -->

% else:

<p>If you are using Ubuntu 14.04 or later, you can use our repository to install the command-line tools (recommended). Otherwise, choose the Tarball Install.</p>

<ul data-tabs class="tabs">
  <li><a data-tab href="#linux-homebrew-install" class="active">• Repository Install</a></li>
  <li><a data-tab href="#linux-tarball-install">• Tarball Install</a></li>
</ul>

</div> <!-- need closing condition div before opening a new one -->

<div data-tabs-content class="data-tabs-content">
  <div data-tabs-pane class="tabs-pane active" id="linux-homebrew-install">
    <h3 class="skip">Repository Install</h3>
    <%include file='get_started/linux_apt_install.html'/>
  </div>

  <div data-tabs-pane class="tabs-pane" id="linux-tarball-install">
    <h3 class="skip">Tarball Install</h3>
    <%include file='get_started/linux_tarball_install.html'/>

    <p>Next, open your terminal and extract the Infinit tarball:</p>

    <pre><code>$> tar xjvf Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}.tbz
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/bin/
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/lib
...
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/bin/infinit-storage
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/bin/infinit-user
Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/bin/infinit-volume
...
</code></pre>

    <p>Now that you’ve extracted the tarball, take a look. The extracted directory contains the following subdirectories:</p>

    <pre><code>$> cd Infinit-x86_64-linux_debian_oldstable-gcc4-${tarball_version}/
$> ls
bin/    lib/    share/
</code></pre>

  </div>
</div>

<div> <!-- open it again -->

% endif
</div>

* The `bin/` subdirectory contains the actual Infinit binaries such as _infinit-user_, _infinit-network_, etc.
* The `lib/` subdirectory contains all the libraries the above binaries depend upon to operate (excluding the FUSE library you installed earlier).
* The `share/infinit/filesystem/test/` subdirectory is provided for you to quickly test the command-line tools, see below.


2. Basic Test
--------------

For you to quickly (through a single command) try Infinit out, the following is going to let you **mount an existing volume** and access files through a mount point. Don’t worry, this "demo" file system only contains test files that we put there ourselves for you to test Infinit. You cannot do any harm to those files since you only have read-only access to this test volume.

First, you need to understand that all the files that the Infinit command-line tools and file system binary create and use are located in the `$INFINIT_HOME` directory which, by default, is set to `$HOME`. More specifically, the configuration files are stored in `$INFINIT_DATA_HOME` (set by default to `$INFINIT_HOME/.local/share/infinit/filesystem/`) while other files such as cached blocks, journaled operations etc. are stored in other directories. All these [environment variables](/documentation/environment-variables) can be changed according to your needs.

In the following, we are going to run the _infinit-volume_ command by prefixing it with the environment variable `INFINIT_DATA_HOME=$PWD/share/infinit/filesystem/test/home` to tell the command where to look for the configuration files required for this test, in this case in the `share/` directory that is next to the binaries and contains the configuration file for this "demo" volume.

```
$> INFINIT_DATA_HOME=$PWD/share/infinit/filesystem/test/home/ ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
...
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
[           infinit-volume           ] [main] client version: 0.5.4
[        infinit.model.Model         ] [main] infinit::model::Model(0x7f7ffbc02bf0): compatibility version 0.3.0
[        infinit.overlay.kelips        ] [main] Running in observer mode
[        infinit.overlay.kelips        ] [main] Filesystem is read-only until peers are reached
[        infinit.overlay.kelips        ] [main] Kelips(0.0.0.0:0): listening on port 62046
Running volume "infinit/demo".
...
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

The first step consists of creating a user on the Hub. All the commands that follow use the user name ‘alice’ but you should **pick your own unique user name**:

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-user --signup --name alice --email alice@company.com --fullname "Alice"
Generating RSA keypair.
Remotely pushed user "alice".
</code></pre>

### Create a storage resource

A storage resource behaves like a hard disk, storing data blocks without understanding the data’s meaning. The beauty of Infinit is that it is completely agnostic of the nature of such storage resources.

Next, we are going to declare a local storage resource. A local storage stores data blocks as files in a directory (such as `/var/storage/infinit/`) on the local filesystem.

The binary _infinit-storage_ is used for this purpose. The option `--filesystem` is used to indicate that the storage will be on the local filesystem:

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-storage --create --filesystem --name local --capacity 1GB
Created storage "local".
</code></pre>

### Create a network

Now that we have at least one storage resource to store data, we can create a network interconnecting different machines.

The _infinit-network_ command is used to create the network, specifying a name along with the list of storage resources to rely upon. We will ignore the other options now but you can read about them in the <a href="${route('doc_reference')}">reference documentation</a>. In this example, only the ‘local’ storage resource is used but you could plug as many as you like. Obviously, you need to substitute 'alice' with your username:

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-network --create --as alice --storage local --kelips --name my-network --push
Locally created network "alice/my-network".
Remotely pushed network "alice/my-network".
</code></pre>

*__NOTE__: The `--push` option is used to publish the created network (likewise for other objects) onto the Hub for it to be easily fetched on another device or shared with other users.*

### Create a volume

The last step on this device consists of creating a logical volume to store and access files. Volumes can be manipulated through the _infinit-volume_ binary as shown next:

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-volume --create --as alice --network my-network --name my-volume --push
Locally created volume "alice/my-volume".
Remotely pushed volume "alice/my-volume".
</code></pre>

That’s it, you’ve created a volume named ‘my-volume’ i.e. a filesystem. The blocks that the files are composed of will be distributed across the network named ‘my-network’, currently composed of a single computer with a single storage resource.

### Mount the volume

Let’s access this volume by mounting it as easily as any other filesystem:

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-volume --mount --as alice --name my-volume --mountpoint ~/mnt-my-volume/ --async --cache --publish
Fetched endpoints for "alice/my-network".
Running network "alice/my-network".
Remotely pushed endpoints for "alice/my-network".
Running volume "alice/my-volume".
...
</code></pre>

_**NOTE**: This command does not return. You can make it run in the background if you prefer. To stop it and unmount the volume, just hit `CTRL^C` or interrupt the process. You should wait until the end of the guide to stop this process though._

That’s it! You can now create, list and access files from the mount point `~/mnt-my-volume`. Try creating a file right now:

<pre class="ribbon"><div><span>Device A</span></div><code>$> echo "everything is" > ~/mnt-my-volume/awesome.txt
$> cat ~/mnt-my-volume/awesome.txt
everything is
</code></pre>

### Access from another machine

Now that you have successfully created and mounted a volume on your machine, it would be interesting to access the data from your other devices. If you don’t have another device, you can simulate another device by opening another terminal and [setting](/documentation/environment-variables) `$INFINIT_HOME` to a different directory.

In order to access your volume from another device, you will need to transfer your user’s identity to that device. A user’s identity is analogous to an SSH key pair and should be treated in the same way. Because of its critical nature, it is not stored on the Hub like network and volume descriptors could be for instance. For this guide, we will rely on the Hub to transmit the identity in encrypted form to the other device.

Let’s transmit the user’s identity to the other device. Note that it will be encrypted with a passphrase chosen by you. Also you will only have 5 minutes to retrieve it on device B.

<pre class="ribbon"><div><span>Device A</span></div><code>$> infinit-device --transmit --user --as alice
Passphrase: ********
Transmitted user identity for "alice".
User identity on the Hub for: 297 seconds
</code></pre>

Now let’s move to device B. First, you need to download, install and configure the Infinit command-line tools on this new device as you did for device A. Please refer to the first sections of this guide for that purpose.

<pre class="alternate"><div><span>Device B</span></div><code>$> infinit-device --receive --user --name alice
Passphrase: ********
Received user identity for "alice".
</code></pre>

_**NOTE**: The pairing process may have expired on device A. If so, please try again and enter the passphrase on device B in order to retrieve your user identity before the counter runs down._

The next steps consist of fetching the resources that you previously pushed on the Hub: networks and volumes.

<pre class="alternate"><div><span>Device B</span></div><code>$> infinit-network --fetch --as alice --name my-network
Fetched network "alice/my-network".
$> infinit-volume --fetch --as alice --name my-volume
Fetched volume "alice/my-volume".
</code></pre>

Let’s link this device to the ‘my-network’ network you created on device A.

<pre class="alternate"><div><span>Device B</span></div><code>$> infinit-network --link --as alice --name my-network
Linked device to network "alice/my-network".
</code></pre>

Finally, the volume can be mounted on device B as simply as on device A:

<pre class="alternate"><div><span>Device B</span></div><code>$> infinit-volume --mount --mountpoint ~/mnt-my-volume2/ --as alice --name my-volume --async --cache --publish
Fetch endpoints for "alice/my-network".
Running network “alice/my-network”.
Running volume "alice/my-volume".
...
</code></pre>

It is now time to check if the file you created on device A is synchronized with device B:

<pre class="alternate"><div><span>Device B</span></div><code>$> ls ~/mnt-my-volume2/
awesome.txt
$> cat ~/mnt-my-volume/awesome.txt
everything is
</code></pre>

That’s it, you’ve created a filesystem that you quickly connected to with two devices, without having to go through a complicated administrative process.

4. Go Further
------------------

<blockquote class="warning">
<strong>Notice:</strong> Some features may not be entirely developed yet, please have a look at <a href="${route('doc_roadmap')}">our roadmap</a> to follow our advancement. You can also subscribe to <a href="http://infinit.us2.list-manage.com/subscribe?u=29987e577ffc85d3a773ae9f0&id=b537e019ee" target="_blank">our newsletter</a> to get notified of our upcoming features.
</blockquote>

You can now invite other users to join the network, to contribute additional storage and/or share files and collaborate. Take a look at the <a href="${route('doc_deployments')}">examples of deployments</a> or the <a href="${route('doc_reference')}">reference documentation</a> to learn how to add additional storage, add more nodes, invite friends and configure the network to be more resilient.

<div>
  <a class="go_deployments" href="${route('doc_deployments')}">
    <img src="${url('images/icons/network.png')}">
    <span>→ Deployment Examples</span>
  </a>

  <a class="go_reference" href="${route('doc_reference')}">
    <img src="${url('images/icons/code.png')}">
    <span>→ Reference Documentation</span>
  </a>
</div>

### Any question, feedback?

Come talk to us! Whether you encounter a problem setting up your infrastructure, have a question or just to give us some feedback, we're never too far away:

* <a href="https://reddit.com/r/infinit" class="icon-reddit">Reddit /r/infinit</a>
* <a href="https://www.facebook.com/groups/1518536058464674/" class="icon-facebook">Facebook Group</a>
* <a href="#slack" class="icon-slack">Slack Community</a>
* <a href="http://webchat.freenode.net/?channels=infinit" class="icon-infinit">IRC Channel</a>
* <a href="https://twitter.com/infinit_sh" class="icon-twitter">Twitter</a>

