FAQ
====

1. How does Infinit differ from IPFS?
-------------------------------------
The goals of the two projects are quite different. IPFS aims to be used to build a peer-to-peer worldwide content platform rather than be a file system. As such it lacks several fundamental file system features such as user permissions. The goal of Infinit is to be a POSIX complaint file system.

2. How does Infinit differ from Dropbox, Google Drive and BitTorrent Sync?
--------------------------------------------------------------------------
Infinit is primarily an enterprise solution, offering system administrators complete control over where their data is stored and how their data storage infrastructure works. That being said, end users will have similar functionality to that found in other cloud storage systems. One of our core values is in making technology accessible which means we've worked hard to make Infinit as easy to configure and use as possible.

3. Where is the open source code?
---------------------------------
You can find the code that we have open sourced in our [GitHub repository](https://github.com/infinit). We plan to open source all the client code for the file system but we would like to do so responsibly. We want to ensure that the code is of high quality and modular enough to be used in other projects.

4. What encryption does Infinit use?
------------------------------------
Infinit uses a mix of RSA and AES encryption. RSA is used for signing data and encrypting AES keys which are used for encrypting block data and communications between nodes.

5. What platforms does Infinit support?
---------------------------------------
Currently Infinit supports Linux and OS X but we have plans for Windows in our [roadmap](http://infinit.sh/documentation/roadmap). If you would like us to support other platforms, [let us know!](http://infinit-sh.uservoice.com)

6. What research is Infinit based on?
-------------------------------------
Infinit is based on the PhD thesis of Julien Quintard (co-founder and CEO of the company): ["Towards a worldwide storage infrastructure"](https://www.repository.cam.ac.uk/bitstream/handle/1810/243442/thesis.pdf?sequence=1).

7. What protocols does Infinit use to communicate?
--------------------------------------------------
Infinit can be configured to use either TCP or UDP ([µTP](https://en.wikipedia.org/wiki/Micro_Transport_Protocol)). By default, it tries to use UDP using UPnP and NAT hole punching where necessary as this is better suited to transferring large amounts of data.

8. Which cloud storage providers does Infinit support?
------------------------------------------------------
Currently Infinit supports Amazon S3 and Google Cloud Storage with Backblaze [planned](http://infinit.sh/documentation/roadmap). Please [let us know](http://infinit-sh.uservoice.com) what else you would like us to support.

9. Do I have to use the server-side 'hub'?
------------------------------------------
No, using the hub facilitates exchanging user public keys and IP addresses of the peers, but it is not required.

10. How does the infinit ACL work?
----------------------------------
To summarize, each file or directory is encrypted using a newly generated AES key. That key is then sealed using the RSA keys of all users who've been granted access. No information about the filesystem layout is leaked to the storage providers.

