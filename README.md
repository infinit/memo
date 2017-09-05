# memo, an elastic and resilient key-value store

<img src="docs/static_files/memo-logotype-docker@2x.png" alt="Logo - Memo" title="Memo logotype" width="300" style="max-width:300px;">

The memo project combines a value store (where you manipulate blocks and addresses) and a key-value store (where you manipulate arbitrary data and arbitrary keys).

`memo` is supported by Docker and is used as backend by the Infinit Storage Platform project.

## What is the difference between the value store and the key-value store

The key-value store uses the value store to provide a higher-level interface like common key-value stores (etcd, ZooKeeper, etc.), where arbitrary data can be stored under an arbitrary name.

The value store is the lowest brick of the architecture, providing the fundamental object named `blocks`, declined in a few flavors. Those blocks are cryptographically protected, their addresses are chosen randomly to guarantee a homogeneous distribution, optimize data placement, fault tolerance and more. All operations are atomic. The main drawback being the responsability of keeping blocks addresses is transfered to the caller.

For more details you can consult [When shoould I use the value store against the key-value store](https://memo.infinit.sh/documentation/overview#value-store).

## How to get memo

To download the source code and build memo by yourself, get it from GitHub.

```bash
git clone https://github.com/infinit/memo --recursive # Clone memo and its submodules.
```

> *Note:* If you cloned it using the GitHub "clone" button, do not forget to run `git submodule update --init --recursive`!

## How to build memo

### Requirements

- [gcc](https://gcc.gnu.org) (>= 4.9.2) or [clang](http://clang.llvm.org) (>= 3.5.0) or [mingw](http://mingw.org) (>= 5.3.0).
- [python3](https://www.python.org/download) (>= 3.4.0) and [pip3](https://pip.pypa.io/en/stable).

#### Core library

`memo` uses [Elle](https://github.com/infinit/drake), Infinit's core library.

#### Build system

`memo` uses [Drake](https://github.com/infinit/drake) and has it as a submodule.

### How to compile

For a detailed procedure, visit our [wiki: How to build](https://github.com/infinit/memo/wiki/How-to-build).

First you need to install Python dependencies.

```bash
sudo pip3 install -r drake/requirements.txt
```
> *Note:* If you don't want dependencies to be installed system-wide, you should consider using [virtualenv](https://virtualenv.pypa.io/en/stable/installation).

Change directory to `_build/<architecture>` where you can find a generic Drake [configuration script](https://github.com/infinit/drake#basic-structures-of-a-drakefile-and-a-drake-script).

#### GNU/Linux

```bash
cd _build/linux64
./drake //build -j 4 # Build everything (using 4 jobs).
```

#### macOS

```bash
cd _build/osx
./drake //build -j 4 # Build everything (using 4 jobs).
```

This will result on `bin/memo`.

## Maintainers

 * Website: https://memo.infinit.sh
 * Email: open+memo@infinit.sh
