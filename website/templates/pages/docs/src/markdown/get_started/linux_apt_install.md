<p>First import the public key used by the package management system:</p>
<pre><code>$> sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 3D2C3B0B
Executing: gpg --ignore-time-conflict --no-options --no-default-keyring --homedir /tmp/tmp.fCTpAPiWSi --no-auto-check-trustdb --trust-model always --keyring /etc/apt/trusted.gpg --primary-keyring /etc/apt/trusted.gpg --keyserver keyserver.ubuntu.com --recv-keys 3D2C3B0B
gpg: requesting key 3D2C3B0B from hkp server keyserver.ubuntu.com
gpg: key 6821EB43: public key "Infinit &lt;contact@infinit.one&gt;" imported
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
</code></pre>

<pre><code>$> sudo apt-get install infinit
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
<p>You can now change to the install directory and <a href='#2--basic-test'>test</a> your install.</p>
<pre><code>$> cd /opt/infinit
</code></pre>