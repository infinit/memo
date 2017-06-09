Docker Volume Plugin
====================

Infinit's Docker volume plugin makes it easy to mount and manage Infinit volumes using Docker.

#### Functionalities

- Mount existing Infinit volumes in Docker containers.

#### Requirements

- Linux.
- Infinit 0.6.2 or later.
- Docker 1.10.0 or later (1.12.0 or later preferred).

<img src="${url('images/schema-docker-volume-plugin.png')}" alt="Schema of the Infinit Docker Volume Plugin">

Running the daemon
------------------

As Docker runs as the *root* user, the `/etc/fuse.conf` file needs to be edited so that Infinit mounts can be accessed by containers. Using a text editor with root privileges, add `user_allow_other` to the file. This only needs to be done once. An example `/etc/fuse.conf` file follows:

```
# /etc/fuse.conf - Configuration file for Filesystem in Userspace (FUSE)

# Set the maximum number of FUSE mounts allowed to non-root users.
# The default is 1000.
#mount_max = 1000

# Allow non-root users to specify the allow_other or allow_root mount options.
user_allow_other

```

The `infinit daemon` binary provides the Docker volume plugin. The binary can either be run in the background as a daemon (using `start` and `stop`) or in the foreground (using `run`).

In order to install the plugin, the daemon must be run as _root_ using `sudo`. The user specified by the `--as` option is the Infinit user we wish to use and the user specified by `--docker-user` is the system user that we would like to use for Docker volume manipulations.

```
$> sudo infinit daemon run --as alice --docker-user alice
[sudo] password for alice:
[  infinit daemon  ] [main] started daemon
```

Once the daemon is running, you can use `docker volume ls` to see a list of your volumes.

```
$> docker volume ls
DRIVER              VOLUME NAME
local               630e7037aeec2601d650dc012cd45d64d4210cf483164a493135b4ce082c5c57
local               bb3c6cd7e5badd09b9ae7fae2134485e0864f4b58350744c395ce5e24ed5da2f
infinit             alice/my-volume

```

Mounting existing volumes in a container
----------------------------------------

Once the plugin is running, Infinit volumes can be mounted as [shared-storage volumes](https://docs.docker.com/engine/tutorials/dockervolumes#mount-a-shared-storage-volume-as-a-data-volume).

```
$> docker run -it --rm --volume-driver infinit -v alice/my-volume:/path/to/mount alpine ash
/ # touch /path/to/mount/something

```

_**IMPORTANT**: Ensure that you have run the volume and accessed it at least once with the `--allow-root-creation` flag._

Volume mount options
--------------------

The options used to mount volumes are those set as the default in the volume descriptor. These can be configured on volume creation or using `infinit volume update`.

```
$> infinit volume update --name alice/my-volume --cache
```

To view the options that have been set, you can export the volume descriptor using `infinit volume --export`:

```
$> infinit volume export --name alice/my-volume
{"mount_options":{"cache":true},"name":"alice/my-volume","network":"alice/my-network"}
```

To disable options that are specified with a boolean, you can set them to `false`, `no` or `0`:

```
$> infinit volume update --name alice/my-volume --cache=false
$> infinit volume export --name alice/my-volume
{"mount_options":{"cache":false},"name":"alice/my-volume","network":"alice/my-network"}
```
