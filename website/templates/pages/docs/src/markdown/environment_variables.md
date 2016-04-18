Environment Variables
=====================

This page contains some environment variables that can be used to alter the file system's behavior.

<blockquote class="warning"><strong>WARNING:</strong> Only use these variables if you precisely understand their impact!</blockquote>

Elle
----

The [Elle](/documentation/technology#elle) library is the core utility library on top of which the Infinit file system is built. This development framework has the particularity to rely on coroutines to allow for asynchronous programming in a natural way.

The Elle-specific environment variables described below are mostly related to logging operations performed by the file system and other low-level components of the whole stack. We would like to encourage developers to use these environment variables to activate the logs when they encounter a problem. If the issue can be reproduced, the whole community would be extremely grateful if the logs could be sent over to the Infinit team for the bug to be fixed.

Do not hesitate to contact the team through the [real-time communication channels](/get-started#any-question--feedback-) in place or by email at <support@infinit.sh>.

### ELLE_LOG_LEVEL ###

The `ELLE_LOG_LEVEL` environment variable modifies the components to log along with the level of verbosity of those logs. The format of the `ELLE_LOG_LEVEL` environment variable is `[component name]:[log level]` e.g `ELLE_LOG_LEVEL="infinit.protocol.RPC:DEBUG,reactor.network*:TRACE,infinit.model*:NONE"`.

Noteworthy is that the '\*' wildcard character can be used to form glob patterns. As such `infinit.*` would activate the components `infinit.filesystem*`, `infinit.model*`, `infinit.cryptography*`, `infinit.protocol*` and many more, following a hierarchical organization.

By default the log level is set to `*:LOG` meaning that all the components are activated with the least verbose log level `LOG`:

```
$> infinit-volume --mount --as alice --name shared
[           infinit-volume           ] [main] client version: 0.5.5-dev
[        infinit.model.Model         ] [main] infinit::model::Model(0x168f2a0): compatibility version 0.5.5
[       infinit.overlay.kelips       ] [main] Filesystem running in bootstrap read/write mode.
[       infinit.overlay.kelips       ] [main] Kelips(10.0.3.1:60441): listening on 10.0.3.1:60441
[       infinit.overlay.kelips       ] [main] Kelips(10.0.3.1:60441): listening on 192.168.0.73:60441
[       infinit.overlay.kelips       ] [main] Kelips(10.0.3.1:60441): listening on 0.0.0.0:60441
```

Below are listed the different log levels one can rely upon:
- `NONE`: Do not display any log for the activated components.
- `LOG`: Only display general information which is displayed in bold and can be of three types:
  - _INFO_: Normal information in white.
  - _WARNING_: Warnings in yellow.
  - _ERROR_: Errors in red.
- `TRACE`: Logs that allow to trace the behavior of the program.
- `DEBUG`: Debug-specific information such as internal states for protocols and algorithms.
- `DUMP`: Most verbose log level that displays internal values of potential consequent size.

Note that the `NONE` level can be used to deactivate a component. For instance `infinit.protocol*:DEBUG:infinit.protocol.RPC:NONE` will activate the `infinit.protocol` components along with its subcomponents, leaving out the `infinit.protocol.RPC`.

Finally, you will find next some of the components that can be activated or not to control the level of logging:
- **Command-line tools**:
  - `infinit-acl`
  - `infinit-block`
  - `infinit-credentials`
  - `infinit-device`
  - `infinit-drive`
  - `infinit-network`
  - `infinit-passport`
  - `infinit-storage`
  - `infinit-user`
  - `infinit-volume`
  - etc.
- **Reactor**: the very core component that provides the coroutine system
  - `reactor.network`
  - `reactor.http`
  - etc.
- **Protocol**: the RPC system
  - `infinit.protocol`
  - `infinit.protocol.RPC`
  - etc.
- **Cryptography**: the cryptographic module
  - `infinit.cryptography`
  - etc.
- **Amazon Web Services**: the AWS' API
  - `aws.CanonicalRequest`
  - `aws.Credentials`
  - `aws.S3`
  - etc.
- **Paxos**: a consensus algorithm
  - `athena.paxos`
  - etc.
- **Overlay**: the overlay network
  - `infinit.overlay`
  - etc.
- **File System**: the file system logic
  - `infinit.filesystem`
  - etc.
- and many more.

The example below activates several components in different log levels to demonstrate the use of the `ELLE_LOG_LEVEL` environment variable:

```
$> ELLE_LOG_LEVEL="infinit.cryptography*:TRACE,infinit.filesystem*:DEBUG" ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
[           infinit-volume           ] [main] client version: 0.5.4-136-g83aa53b
[    infinit.cryptography.rsa.low    ] [main] RSA_dup(0x7f8bb3430140)
[    infinit.cryptography.rsa.low    ] [main] RSA_dup(0x7f8bb3431540)
[    infinit.cryptography.rsa.low    ] [main] RSA_dup(0x7f8bb3431d30)
[    infinit.cryptography.rsa.low    ] [main] RSA_dup(0x7f8bb3431d30)
[        infinit.model.Model         ] [main] infinit::model::Model(0x7f8bb3700830): compatibility version 0.3.0
[    infinit.cryptography.rsa.low    ] [main] RSA_dup(0x7f8bb3431d30)
[        infinit.overlay.kelips        ] [main] Running in observer mode
[        infinit.overlay.kelips        ] [main] Filesystem is read-only until peers are reached
[        infinit.overlay.kelips        ] [main] Kelips(0.0.0.0:0): listening on port 64642
[      infinit.cryptography.random      ] generate()
[      infinit.cryptography.random      ]   generate(24)
[  infinit.cryptography.rsa.PrivateKey  ] sign(pss, sha256) <PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960) at PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960)>
[  infinit.cryptography.rsa.PrivateKey  ]   sign(pss, sha256) <PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960) at PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960)>
[      infinit.cryptography.Oneway      ]     resolve(sha256)
[   infinit.cryptography.rsa.Padding    ]     resolve(pss)
Running volume "infinit/demo".
[     infinit.cryptography.rsa.low      ] [main] RSA_dup(0x7f8bb3431d30)
[       infinit.cryptography.hash       ] hash(sha256)
[       infinit.cryptography.hash       ]   hash(sha256)
[      infinit.cryptography.Oneway      ]     resolve(sha256)
[      infinit.cryptography.random      ] generate()
[      infinit.cryptography.random      ]   generate(24)
[  infinit.cryptography.rsa.PrivateKey  ] sign(pss, sha256) <PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960) at PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960)>
[  infinit.cryptography.rsa.PrivateKey  ]   sign(pss, sha256) <PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960) at PrivateKey(0x308204a3020100028201...0x26a0a43fd0e60098d960)>
[      infinit.cryptography.Oneway      ]     resolve(sha256)
[   infinit.cryptography.rsa.Padding    ]     resolve(pss)
...
```

Note that depending on the verbosity of the logs, it may have an impact on the performance. As such, we advise for the log level to be set, when used on a regular basis, to something close to `ELLE_LOG_LEVEL="infinit.filesystem*:DEBUG,infinit.model*:DEBUG`.

### ELLE_LOG_FILE ###

The `ELLE_LOG_FILE` takes a path as argument that references the file in which the logs will be stored rather than being displayed on the standard error (`stderr`) stream.

```
$> ELLE_LOG_FILE=$PWD/demo.log ELLE_LOG_LEVEL="infinit.protocol*:DEBUG,infinit.filesystem*:DEBUG" ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
Running volume "infinit/demo".
...
```

The logs have been, as expected, stored in the file referenced through the `ELLE_LOG_FILE` environment variable:

```
$> cat demo.log
[           infinit-volume           ] [main] client version: 0.5.4-136-g83aa53b
[        infinit.model.Model         ] [main] infinit::model::Model(0x7ffd2b714c30): compatibility version 0.3.0
[        infinit.overlay.kelips        ] [main] Running in observer mode
[        infinit.overlay.kelips        ] [main] Filesystem is read-only until peers are reached
[        infinit.overlay.kelips        ] [main] Kelips(0.0.0.0:0): listening on port 62729
[            infinit.filesystem            ] [fuse worker] infinit::filesystem::FileSystem(0x7ffd2b714e60): fetch root
[            infinit.filesystem            ] [fuse worker]   root block cache: "/Users/mycure/.local/state/infinit/filesystem/infinit/demo/infinit/demo/root_block"
[            infinit.filesystem            ] [fuse worker]   fetch root bootstrap block at 0xd14ef7f708
[            infinit.filesystem            ] [fuse worker]     fetch root block at 0xba2d0e2497
[          infinit.overlay.kelips          ] [listener] Peer found, write enabled
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection] ChanneledStream 0x7ffd2b7250a0: handshake to determine master
[       infinit.protocol.Serializer        ] [dht::Remote(0xa5d5af9250) connection]   Serializer 0x7ffd2b43c180: send 0xcb
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection]   ChanneledStream 0x7ffd2b7250a0: my roll: -53
[       infinit.protocol.Serializer        ] [dht::Remote(0xa5d5af9250) connection]   Serializer 0x7ffd2b43c180: read packet
[       infinit.protocol.Serializer        ] [dht::Remote(0xa5d5af9250) connection]     Serializer 0x7ffd2b43c180: packet size: 1
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection]   ChanneledStream 0x7ffd2b7250a0: his roll: -40
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection]   ChanneledStream 0x7ffd2b7250a0: slave
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection] ChanneledStream 0x7ffd2b7250a0: open channel 0
[         infinit.protocol.Channel         ] [dht::Remote(0xa5d5af9250) connection] ChanneledStream 0x7ffd2b7250a0: open channel -1
...
```

### ELLE_LOG_PID ###
<h3 class="list">ELLE_LOG_TID</h3>

The environment variables `ELLE_LOG_PID` and `ELLE_LOG_TID` can be activated, i.e set to `1`, to include in the logs the identifier of the process and thread (i.e coroutine), respectively.

In the example below, only the `ELLE_LOG_PID` variable is activated, resulting in a section in the logs, following the component name, indicating the process identifier, in this case `29607`:

These environment variables are particularly helpful when logs from different programs are written to the same file.

```
$> ELLE_LOG_PID=1 ELLE_LOG_LEVEL="infinit.protocol*:DEBUG,infinit.filesystem*:DEBUG" ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
[           infinit-volume           ] [29607] [main] client version: 0.5.4-136-g83aa53b
[        infinit.model.Model         ] [29607] [main] infinit::model::Model(0x7ffd81c0c310): compatibility version 0.3.0
[        infinit.overlay.kelips        ] [29607] [main] Running in observer mode
[        infinit.overlay.kelips        ] [29607] [main] Filesystem is read-only until peers are reached
[        infinit.overlay.kelips        ] [29607] [main] Kelips(0.0.0.0:0): listening on port 59871
Running volume "infinit/demo".
[            infinit.filesystem            ] [29607] [fuse worker] infinit::filesystem::FileSystem(0x7ffd81d32af0): fetch root
[            infinit.filesystem            ] [29607] [fuse worker]   root block cache: "/Users/mycure/.local/state/infinit/filesystem/infinit/demo/infinit/demo/root_block"
[            infinit.filesystem            ] [29607] [fuse worker]   fetch root bootstrap block at 0xd14ef7f708
[            infinit.filesystem            ] [29607] [fuse worker]     fetch root block at 0xba2d0e2497
[          infinit.overlay.kelips          ] [29607] [listener] Peer found, write enabled
[         infinit.protocol.Channel         ] [29607] [dht::Remote(0xa5d5af9250) connection] ChanneledStream 0x7ffd81d3e940: handshake to determine master
[       infinit.protocol.Serializer        ] [29607] [dht::Remote(0xa5d5af9250) connection]   Serializer 0x7ffd81f1cfc0: send '
...
```

### ELLE_LOG_TIME ###

The `ELLE_LOG_TIME` environment variables can be set to `1` to add a section in the logs that displays the current time. This option is particularly useful when trying to understand why some operations take a long time.

```
$> ELLE_LOG_TIME=1 ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
2016-Mar-18 13:23:13: [           infinit-volume           ] [main] client version: 0.5.4-136-g83aa53b
2016-Mar-18 13:23:13: [        infinit.model.Model         ] [main] infinit::model::Model(0x7fe7795106b0): compatibility version 0.3.0
2016-Mar-18 13:23:13: [        infinit.overlay.kelips        ] [main] Running in observer mode
2016-Mar-18 13:23:13: [        infinit.overlay.kelips        ] [main] Filesystem is read-only until peers are reached
2016-Mar-18 12:15:16: [infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7feb1c004000) init]   Failed to reload 1877: end of stream while reading number
2016-Mar-18 12:15:16: [infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7feb1c004000) init]   Failed to reload 1878: end of stream while reading number
...
```

### ELLE_LOG_DISPLAY_TYPE ###

You can have the type of message specified in the logs: _INFO_, _WARNING_ or _ERROR_. This is useful when logging to a file in which the colors (white, yellow and red in bold) will not be easily detectable.

```
$> ELLE_LOG_DISPLAY_TYPE=1 ./bin/infinit-volume --mount --as demo --name infinit/demo --mountpoint ~/mnt-demo/ --publish --cache
Fetched endpoints for "infinit/demo".
Running network "infinit/demo".
[           infinit-volume           ] [main] client version: 0.5.4-136-g83aa53b
[        infinit.model.Model         ] [main] infinit::model::Model(0x7fe7795106b0): compatibility version 0.3.0
[        infinit.overlay.kelips        ] [main] Running in observer mode
[        infinit.overlay.kelips        ] [main] Filesystem is read-only until peers are reached
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7feb1c004000) init]   Failed to reload 1877: end of stream while reading number
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7feb1c004000) init]   Failed to reload 1878: end of stream while reading number
...
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1861: elle::serialization::binary::SerializerIn(0x10d0824f0): short read when deserializing 'opaddr': expected 942439, got 69
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1863: end of stream while reading number
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1864: end of stream while reading number
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1865: end of stream while reading number
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1867: elle::serialization::binary::SerializerIn(0x10d0824f0): short read when deserializing 'opaddr': expected 746873, got 42
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1868: end of stream while reading number
[infinit.model.doughnut.consensus.Async ] [dht::consensus::Async(0x7fc2d5809800) init] [warning] Failed to reload 1869: elle::serialization::binary::SerializerIn(0x10d0824f0): short read when deserializing 'opaddr': expected 225136, got 47
...
```

### ELLE_REAL_ASSERT ###

`ELLE_REAL_ASSERT` can be set to `1`, to indicate the software to `abort()` should an assertion fail rather than throw an exception which is the default behavior.

Infinit
-------

Infinit is composed of many layers, from the [overlay network](/documentation/technology#overlay-network), the [distributed hash table](/documentation/technology#distributed-hash-table), the [file system](/documentation/technology#file-system) logic up to the POSIX-compliance layer and the command-line tools and graphical user interfaces.

### INFINIT_USER ###

Specifying the username of the user the software should operate on behalf of is sometimes cumbersome, in particular when dealing with a single user.

The `INFINIT_USER` environment variable can be specified with the name of a user, effectively replacing `--as <username>` for every command line.

### INFINIT_BACKTRACE ###

The `INFINIT_BACKTRACE` environment variable, if set to `1`, indicates the software to display a backtrace should an exception escape.

### INFINIT_HOME ###
<h3 class="list">INFINIT_DATA_HOME</h3>
<h3 class="list">INFINIT_CACHE_HOME</h3>
<h3 class="list">INFINIT_STATE_HOME</h3>

By default, all the information the command-line tools and GUI need are looked for in the `$HOME/.local/...` directories following the [XDG Base Directory Specifications](http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html).

The `INFINIT_HOME` environment variable can be used to define a new user home directory. For instance, should `INFINIT_HOME` be set to `/tmp/home.test/`, Infinit would be looking for configuration files, cache directories and journals in the directory `/tmp/home.test/.local/...`.

This comes in handy when you only own a single device but want to <strong>simulate another device</strong> by opening another terminal and setting `INFINIT_HOME` to a different directory. You can then simulate as many devices as you want.

Several more specific environment variables are also provided:
- `INFINIT_DATA_HOME`: Path to the specific directory containing the user identities, network and volumes descriptors etc.
- `INFINIT_CACHE_HOME`: Path to a directory containing cached information such as data blocks, avatars, icons etc.
- `INFINIT_STATE_HOME`: Path to stateful data, more specifically the journal's operations that remain to be published onto the network.

### INFINIT_CRASH_REPORTER ###

The crash reporter can be activated or deactivated by setting to `1` or `0` respectively the `INFINIT_CRASH_REPORTER` environment variable.

### INFINIT_RDV ###

Infinit uses a server for NAT traversal. If you would like to disable this, you can set the `INFINIT_RDV` variable to an empty string.

<!-- ???
 'INFINIT_ASYNC_MAX_ENTRY_HOPS',
 'INFINIT_ASYNC_NOPOP',
 'INFINIT_ASYNC_THREADS',
 'INFINIT_CONNECT_TIMEOUT',
 'INFINIT_DISABLE_PEER_CACHE',
 'INFINIT_DISABLE_SIGNAL_HANDLER',
 'INFINIT_KELIPS_ASYNC',
 'INFINIT_KELIPS_ASYNC_SEND',
 'INFINIT_LOOKAHEAD_BLOCKS',
 'INFINIT_LOOKAHEAD_THREADS',
 'INFINIT_PAXOS_LENIENT_FETCH',
 'INFINIT_PEER_CACHE_DUP',
 'INFINIT_PREFETCH_DEPTH',
 'INFINIT_PREFETCH_THREADS',
 'INFINIT_PRESERVE_ACLS',
 'INFINIT_RPC_DISABLE_CRYPTO',
 'INFINIT_SOFTFAIL_FAST',
 'INFINIT_SOFTFAIL_TIMEOUT',
-->
