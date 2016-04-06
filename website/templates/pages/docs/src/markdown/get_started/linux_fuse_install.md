<p>Infinit relies on <a href="https://en.wikipedia.org/wiki/Filesystem_in_Userspace">FUSE</a> to create filesystems in userland. You will need to install FUSE using your distribution's package manager. For example, if you use a Debian based distribution, you would use <code>apt-get</code>:</p>

<pre><code>$> sudo apt-get install fuse
</code></pre>

<p>Some distributions, such as Red Hat Enterprise Linux and CentOS, require that you add your user to the <em>fuse</em> group. You will only need to do this step if you get a <em>permission denied</em> error when trying to mount a volume. After adding the user to the group, you will need to logout and back in again.</p>
<pre><code>$> sudo usermod -a -G fuse $USER
</code></pre>
