<p>First import the public key used by the package management system:</p>
<pre><code>$> sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 3D2C3B0B
...
gpg: key 6821EB43: public key "Infinit &lt;contact@infinit.one&gt;" imported
</code></pre>

<p>Ensure that you have the <code>add-apt-repository</code> command installed:
<pre><code>$> sudo apt-get install software-properties-common
Reading package lists... Done
Building dependency tree
Reading state information... Done
software-properties-common is already the newest version.
0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.
</code></pre>

<p>Then add the repository locally:</p>
<pre><code>$> sudo add-apt-repository "deb https://debian.infinit.sh/ trusty main"
</code></pre>

<p>Finally, you can update your local list of packages and install the command-line tools as you would any other package:</p>

<pre><code>$> sudo apt-get update
$> sudo apt-get install infinit
...
Unpacking infinit (${tarball_version})...
Setting up infinit (${tarball_version})...
</code></pre>
