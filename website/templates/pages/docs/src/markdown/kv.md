The Infinit Key-Value store
===========================

Infinit provides a distributed decentralized key-value store, with built-in
replication and security. This key-value store is accessible through a [grpc](http://www.grpc.io) interface
specified in the file `doughnut.proto`.

## API overview ##

At its most basic level, a key-value store provides a mapping from an Address to
its corresponding content. The API is comprised of four main methods:

* `BlockOrException fetch(Address)`: Returns the data Block found at given address or an error.
* `EmptyOrException remove(Address)`: Erase the data Block at given address.
* `EmptyOrException insert(Block)`: Insert a new block into the KV store, and return
  the address that was attributed to it.
* `EmptyOrException update(Block)`: Update a data block without changing its address.

One thing of importance should be noted at this point: in the infinit KV-store
one cannot chose the address that will be allocated to a given block: the
address is allocated by infinit in a way that prevents unauthorized users
or compromised storage nodes from tampering with the content.

## Block types ##

The infinit KV store supports many different block types that serve different
purposes. The most important ones are:

* __Immutable blocks (IB)__: The simplest of all blocks, whose address is simply the hash of their content.
* __Mutable Block (MB)__: Basic mutable block, providing atomic update (read-then-write)
  using a versioning mechanism, and security since the payload is encrypted.
* __Address Control List Block (ACLB)__: Refinement over mutable blocks providing a
  fined-grained ACL system which can be used to defined which users can read and
  write the block content.
* __Named Block (NB)__: Blocks whose address can be deduced from their unique name.
  They typically store the address of another MB as their payload.

### The Block message type ###

Blocks are read and written on the KV store through the `Block` protobuf message
type. The most important fields are:

* `bytes address`: The address of the Block.
* `bytes data`: The raw data payload contained in the block.
* `bytes data_plain`: The decyphered data payload (for mutable blocks which are encyphered by the kv store).

Block contains more field whose meaning depends on the block type.

### Immutable blocks (IB) ###

The IB is an immutable block (it cannot be updated) whose address is the hash of
its content. This feature ensures the data is tamper-proof: when fetching an IB
through the `fetch` API call, infinit will recompute the hash of the content and check
it against the address. If both hash do not match `fetch` will return
an exception.

IB exposes one additional feature through the `block.owner` field:
if set to the address of another mutable block, then removing the IB will only be allowed if
the user would be allowed to remove said mutable block.

### Mutable blocks (MB) ###

Mutable blocks are versioned data blocks that can be updated.

Infinit enforces an atomic update scheme by using a versioning mechanism:
When you `fetch` a mutable block, the returned `Block` contains a version field
that contains the block version at the moment the read occured.
When you attempt to call `update`, that version gets incremented by one, and
the update will only succeed if the resulting number is above the current
block version. Otheriwise the `update` call will fail with an exception.
In that case the returned `Exception` object will contain the current
value of the block in the KV store, in the `current` field.

Atomic updates is an important feature. For instance if your MB's payload is
a list of values and you want to add one item
to that list, you need to handle the case where another task is also making an
update to the list at the same time. See the [example](#example) below for an
illustration of this use case.

### ACL blocks (ACLB) ###

ACLB inherits the features of mutable blocks and provide additional fields to control
which users can read and write the data.

By default an ACLB can only be read and written by the user who created the block.

The `Block` message has the following fields used to control ACLs:

* `world_readable`: If true all users will be allowed to read the ACLB payload.
* `world_writable`: If true all users will be allowed to update the ACLB payload.
* `acl`: A list of `ACLEntry`.

The `ACLEntry` message exposes the following fields:

* `key_koh`: user concerned by this ACL. This field containes the serialized public key of the user.
* `read`: give read access to the user.
* `write`: give write access to the user.

### Named blocks (NB) ###

Named Blocks have the unique feature of being accessible from a user-defined unique-id.
They are typically used as an entry point to access further data, usually by storing
the address of an other mutable block in their payload.

NBs are currently Immutable and thus cannot be updated once created.
Use the `named_block_address(NamedBlockKey)` function to obtain the NB block address from its unique id.

## Creating and inserting new blocks ##

When inserting new blocks into the key-value store, one first needs to obtain
a `Block` message through one of the builder functions:

* Block make_immutable_block(CHBData) : create IB with given payload and owner
* Block make_mutable_block(Empty) : create MB (no arguments)
* Block make_acl_block(Empty) : create ACLB (no arguments)
* Block make_named_block(NamedBlockKey) : create NB with given key

You can then fill the `data` (IB) or `data_plain` (MB) field with your payload and
call the `insert` function.

## Example##

*A simple multi-user document storage system*

This section provides a complete example of a simple use case of the infinit KV store. Our task for this project is to provide a simple document storage system for multiple users.

### Public API ###

Our goal is to implement the following simple API:

<pre class="goal">
<code class="notInFullCode">void create_user(string user);
vector<string> list_documents(string user);
string get_document(string user, string name);
void set_document(string user, string name, string data);
</code>
</pre>

### Specifying our block layout ###

We will use the following blocks:

* For each user, one [NB](#NB) keyed with the user name will contain the address
  of the document list block.
* For each user, one [MB](#MB) will serve as the document list. Its payload will
  be a serialized map of document names to the document content address.
* For each document, one [IB](#IB) will store the document content.

For the document list format, we will use a simple text serialization scheme of the form
`"name=address\nname2=address2\n..."`. We will encode addresses in hexadecimal.

### Generating the gRPC and protobuf sources ###

The first step is to generate the sources from the [doughnut.proto] file. This can
be achieved by the following two commands:

<ul class="switchLanguage">
  <li><a class="active" data-language="go" href="#">Go</a></li>
  <li><a data-language="cpp" href="#">C++</a></li>
  <li><a data-language="python" href="#">Python</a></li>
</ul>

<pre>
<code class="lang-python notInFullCode">$> protoc -I path --python_out=. --grpc_python_out=.  --plugin=protoc-gen-grpc_python=$(which grpc_python_plugin) path/doughnut.proto
</code>
</pre>

<pre>
<code class="lang-go notInFullCode">$> mkdir -p doughnut/src/doughnut
$> protoc -I path --go_out=plugins=grpc:doughnut/src/doughnut path/doughnut.proto
</code>
</pre>

<pre>
<code class="c++ notInFullCode">$> protoc -I/path --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` /path/doughnut.proto
$> protoc -I/path --cpp_out=. /path/doughnut.proto
</code>
</pre>

... where *path* is the path where *doughnut.proto* is stored. This step will generate a few source and headers file depending on the language.


### Connecting to the gRPC KV store server ###

Let us start with the boilerplate needed to connect to the grpc server. We will
accept the grpc endpoint name into the first command line argument.
We also take a command from *create*, *list*, *get*, *set* and its arguments from the
command line.

```python
#! /usr/bin/env python3

import sys
import codecs
import doughnut_pb2_grpc
import grpc

import doughnut_pb2 as doughnut

# create the stubs with:
# python -m grpc_tools.protoc -I../../protos --python_out=. --grpc_python_out=. doughnut.proto
def to_hex(s):
  return "".join("{:02x}".format(c) for c in s)

def from_hex(h):
  return codecs.decode(h, "hex")

channel = grpc.insecure_channel(sys.argv[1])
stub = doughnut_pb2_grpc.DoughnutStub(channel)

command = sys.argv[2]
if command == 'create':
  create_user(sys.argv[3])
elif command == 'list':
  print("\n".join(list_documents(sys.argv[3])))
elif command == 'get':
  print(get_document(sys.argv[3], sys.argv[4]))
elif command == 'set':
  set_document(sys.argv[3], sys.argv[4], sys.argv[5])
```

```go
package main

import (
  "encoding/hex"
  "fmt"
  "os"
  "strings"
  "doughnut"
  "golang.org/x/net/context"
  "google.golang.org/grpc"
  "google.golang.org/grpc/grpclog"
)

func from_hex(input string) []byte {
  v, _ := hex.DecodeString(input)
  return v
}

func to_hex(input []byte) string {
  return hex.EncodeToString(input)
}

func main() {
  conn, err := grpc.Dial(os.Args[1], grpc.WithInsecure())
	if err != nil {
		grpclog.Fatalf("fail to dial: %v", err)
	}
	defer conn.Close()
	client := doughnut.NewDoughnutClient(conn)
	cmd := os.Args[2]
	if cmd == "create" {
	  create_user(client, os.Args[3])
	}
	if cmd == "list" {
	  fmt.Printf("%v\n", list_documents(client, os.Args[3]))
	}
	if cmd == "get" {
	 fmt.Println(string(get_document(client, os.Args[3], os.Args[4])))
	}
	if cmd == "set" {
	  set_document(client, os.Args[3], os.Args[4], []byte(os.Args[5]))
	}
}
```

```c++
#include <vector>
#include <string>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>

#include <doughnut.grpc.pb.h>
#include <grpc++/grpc++.h>

// Connection to the grpc server
std::shared_ptr<Doughnut::Stub> kv;

using std::vector;
using std::string;
using std::map;

// our API
void create_user(string name);
vector<string> list_documents(string name);
string get_document(string user, string name);
void set_document(string user, string name, string data);

// helper functions
using boost::algorithm::hex;
using boost::algorithm::unhex;

int main(int argc, char** argv)
{
    // create the connection to the infinit kv grpc server
    auto chan = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
    // instanciate the client stub whose code was autogenerated
    kv = Doughnut::NewStub(chan);
    string cmd = argv[2];
    if (cmd == "create") {
      create_user(argv[3]);
      std::cout << "OK" << std::endl;
    }
    else if (cmd == "list") {
      vector<string> docs = list_documents(argv[3]);
      std::cout << boost::algorithm::join(docs, "\n") << std::endl;
    }
    else if (cmd == "get")
      std::cout << get_document(argv[3], argv[4]) << std::endl;
    else if (cmd == "set")
      set_document(argv[3], argv[4], argv[5]);
}
```

### create_user ###

Implementing `create_user` is easy: we first create a Mutable Block with an empty payload,
which will be our document list block. We then create a Named Block keyed with our user
name, that points to the document list block.

```python
def create_user(user):
  # Create and insert the document list MB
  doclist = stub.make_mutable_block(doughnut.Empty())
  stub.insert(doughnut.Insert(block=doclist))
  # Create a NB keyed by the user name
  nb = stub.make_named_block(doughnut.NamedBlockKey(key=user.encode('utf-8')))
  nb.data = doclist.address
  res = stub.insert(doughnut.Insert(block=nb))
  if res.HasField('exception_ptr'):
    raise Exception('User already exists: %s' % res.exception_ptr.exception.message)

```

```go
func create_user(client doughnut.DoughnutClient, user string) {
  doclist,_ := client.MakeMutableBlock(context.Background(), &doughnut.Empty{})
  client.Insert(context.Background(), &doughnut.Insert{Block: doclist})

  nb, _ := client.MakeNamedBlock(context.Background(),
    &doughnut.NamedBlockKey{Key: []byte(user)})
  nb.Payload = &doughnut.Block_Data{doclist.Address}
  res, _ := client.Insert(context.Background(), &doughnut.Insert{Block: nb})
  if res.GetExceptionPtr() != nil {
    fmt.Printf("User already exists")
  }
}
```

```c++
void create_user(string user)
{
  // Create an insert message
  ::Insert insert;
  // Fill it with a new mutable block
  ::Empty empty;
  {
     grpc::ClientContext ctx;
     kv->make_mutable_block(&ctx, empty, insert.mutable_block());
  }
  // Call the insert method
  {
    grpc::ClientContext ctx;
    ::EmptyOrException res;
    kv->insert(&ctx, insert, &res);
  }
  // Create a new named block with key the user name
  ::NamedBlockKey key;
  ::Block nb;
  key.set_key(user);
  {
    grpc::ClientContext ctx;
    kv->make_named_block(&ctx, key, &nb);
  }
  // Set it's payload to the address of the document list mutable block
  nb.set_data(doclist.address());
  // Insert the block into the kv store
  {
    ::EmptyOrException res;
    ::Insert insert;
    insert.mutable_block()->CopyFrom(nb);
    grpc::ClientContext ctx;
    kv->insert(&ctx, insert, &res);
    if (res.has_exception_ptr())
      throw std::runtime_error("User already exists");
  }
}
```

### list_documents ###

Let's first factor the serialization functions since we
are going to need them multiple times.

```python
def parse_document_list(payload):
  lines = payload.split('\n')
  res = dict()
  for l in lines:
    kv = l.split('=')
    if len(kv) == 2:
      res[kv[0]] = kv[1]
  return res

def serialize_document_list(dl):
  res = ''
  for k,v in dl.items():
    res += '%s=%s\n' % (k, v)
  return res
```


```go
func parse_document_list(payload string) map[string]string {
  m := make(map[string]string)
  lines := strings.Split(payload, "\n")
  for _, l := range lines {
    kv := strings.Split(l, "=")
    if len(kv) == 2 {
      m[kv[0]] = kv[1]
    }
  }
  return m
}

func serialize_document_list(dl map[string]string) string {
  res := ""
  for k, v := range dl {
    res += fmt.Sprintf("%s=%s\n", k, v)
  }
  return res
}
```

```c++
map<string, string> parse_document_list(string payload)
{
  vector<string> lines;
  boost::algorithm::split(lines, payload, boost::algorithm::is_any_of("\n"));
  map<string, string> res;
  for (string const& l: lines)
  {
    auto p = l.find_first_of("=");
    res.insert(std::make_pair(l.substr(0, p), l.substr(p+1)));
  }
  return res;
}

string serialize_document_list(map<string, string> dl)
{
  string res;
  for (auto const& d: dl)
  {
    res += d.first + "=" + d.second + "\n";
  }
  return res;
}
```

To list a user's document. We need to first fetch it's Named Block. The NB payload
will be the address of the document list block.

Let's factor the get part since we'll need it multiple times.

```python
def get_documents(user):
  addr = stub.named_block_address(doughnut.NamedBlockKey(key=user.encode('utf-8')))
  nb_or_err = stub.fetch(doughnut.Fetch(address = addr.address))
  if nb_or_err.HasField('exception_ptr'):
    raise Exception('No such user')
  block_or_err = stub.fetch(doughnut.Fetch(address = nb_or_err.block.data, decrypt_data = True))
  b = doughnut.Block()
  b.CopyFrom(block_or_err.block)
  return (parse_document_list(block_or_err.block.data_plain.decode()), b)

def list_documents(user):
  dl,unused = get_documents(user)
  return dl.keys()
```

```go
func get_documents(client doughnut.DoughnutClient, user string) (map[string]string, doughnut.Block) {
  addr,_ := client.NamedBlockAddress(context.Background(),
    &doughnut.NamedBlockKey{Key: []byte(user)})
  nb_or_err,_ := client.Fetch(context.Background(),
    &doughnut.Fetch{Address: addr.Address})
  if nb_or_err.GetExceptionPtr() != nil {
    fmt.Println("No such user", user, ": ", string(nb_or_err.GetExceptionPtr().Exception.Message))
    return make(map[string]string), doughnut.Block{}
  }
  block_or_err,_ := client.Fetch(context.Background(),
    &doughnut.Fetch{Address: nb_or_err.GetBlock().GetData(), DecryptData: true})
  return parse_document_list(
    string((*block_or_err.GetBlock()).GetDataPlain())), *block_or_err.GetBlock()
}

func list_documents(client doughnut.DoughnutClient, user string) []string {
  dl, _ := get_documents(client, user)
  res := make([]string, len(dl))
  i := 0
  for k, _ := range(dl) {
    res[i] = k
    i++
  }
  return res
}
```

```c++
map<string, string> get_documents(string user, ::Block* block = nullptr)
{
  ::Address address;
  ::NamedBlockKey key;
  key.set_key(user);
  // Get the NB address
  {
    grpc::ClientContext ctx;
    kv->named_block_address(&ctx, key, &address);
  }
  // Get the NB
  ::BlockOrException res;
  {
    grpc::ClientContext ctx;
    ::Fetch fetch;
    fetch.set_address(address.address());
    kv->fetch(&ctx, fetch, &res);
    if (res.has_exception_ptr())
      throw std::runtime_error("No such user (" + res.exception_ptr().exception().message() + ")");
  }
  // Get the document list MB
  address.set_address(res.block().data());
  {
    grpc::ClientContext ctx;
    ::Fetch fetch;
    fetch.set_address(res.block().data());
    fetch.set_decrypt_data(true);
    kv->fetch(&ctx, fetch, &res);
  }
  if (block)
    block->CopyFrom(res.block());
  auto dl = parse_document_list(res.block().data_plain());
  return dl;
}

vector<string> list_documents(string user)
{
  auto dl = get_documents(user);
  vector<string> list;
  for (auto const& d: dl)
    list.push_back(d.first);
  return list;
}
```

### get_document ###

To get the content of one specific document, we use the above `get_documents` function,
extract the document address, and fetch its IB:

```python
def get_document(user, name):
  dl, unused = get_documents(user)
  hexaddr = dl.get(name, None)
  if not hexaddr:
    raise Exception('No such document')
  doc = stub.fetch(doughnut.Fetch(address = from_hex(hexaddr)))
  return doc.block.data
```

```go
func get_document(client doughnut.DoughnutClient, user string, name string) []byte {
  dl, _ := get_documents(client, user)
  addr := from_hex(dl[name])
  doc, _ := client.Fetch(context.Background(), &doughnut.Fetch{Address: addr})
  return doc.GetBlock().GetData()
}
```

```c++
string get_document(string user, string name)
{
  auto dl = get_documents(user);
  auto it = dl.find(name);
  if (it == dl.end())
    throw std::runtime_error("no such document");
  ::BlockOrException res;
  grpc::ClientContext ctx;
  ::Fetch fetch;
  fetch.set_address(unhex(it->second));
  kv->fetch(&ctx, fetch, &res);
  return res.block().data();
}
```

### set_document ###

To create or update a document, we first need to create a new IB with the document
content, and then add it to the user document list.
Here comes the trickiest part: we need to update the document list atomically, in
case two tasks try to update said document list at the same time. For that we will
retry the update until there is no update conflict.

```python
def set_document(user, name, data):
  # create and insert the document IB
  doc = stub.make_immutable_block(doughnut.CHBData(data=data.encode('utf-8')))
  stub.insert(doughnut.Insert(block=doc))
  blockaddr = to_hex(doc.address)
  dl, block = get_documents(user)
  while True:
    # If name is already in the document list, cleanup the data IB
    prev = dl.get(name, None)
    if prev:
      pass #stub.remove(doughnut.Address(address = from_hex(prev)))
    # play or re-play our operation into dl
    dl[name] = blockaddr
    block.data_plain = serialize_document_list(dl).encode('utf-8')
    status = stub.update(doughnut.Update(block = block, decrypt_data = True))
    if not status.HasField('exception_ptr'):
      break
    if not status.exception_ptr.exception.HasField('current'):
      raise Exception(status.exception_ptr.exception.message)
    block.CopyFrom(status.exception_ptr.exception.current)
    dl = parse_document_list(block.data_plain.decode())
```

```go
func set_document(client doughnut.DoughnutClient, user string, name string, data []byte) {
  // create and insert the document IB
  doc, _ := client.MakeImmutableBlock(context.Background(),
    &doughnut.CHBData{Data: data})
  client.Insert(context.Background(), &doughnut.Insert{Block: doc})
  blockaddr := to_hex(doc.Address)
  dl, block := get_documents(client, user)
  for {
    // If name is already in the document list, cleanup the data IB
    if val, ok := dl[name]; ok {
      client.Remove(context.Background(), &doughnut.Address{Address: from_hex(val)})
    }
    // play or re-play our operation into dl
    dl[name] = blockaddr
    payload := []byte(serialize_document_list(dl))
    block.Payload = &doughnut.Block_DataPlain{payload}
    status, _ := client.Update(context.Background(),
      &doughnut.Update{Block: &block, DecryptData: true})
    if status.GetExceptionPtr() == nil {
      break
    }
    if status.GetExceptionPtr().Exception.GetCurrent() == nil {
      fmt.Printf("fatal exception %s", status.GetExceptionPtr().Exception.Message)
      break
    }
    block = *status.GetExceptionPtr().Exception.Current
    dl = parse_document_list(string(block.GetDataPlain()))
  }
}
```

```c++
void set_document(string user, string name, string data)
{
  ::Block mb;
  auto dl = get_documents(user, &mb);
  // Create and insert the document IB
  ::CHBData cdata;
  cdata.set_data(data);
  ::Block doc;
  {
    grpc::ClientContext ctx;
    kv->make_immutable_block(&ctx, cdata, &doc);
  }
  ::EmptyOrException status;
  {
    grpc::ClientContext ctx;
    ::Insert insert;
    insert.mutable_block()->CopyFrom(doc);
    kv->insert(&ctx, insert, &status);
  }
  string chb_addr = hex(doc.address());
  while (true)
  {
    // If name is already in the document list, cleanup the data IB
    auto it = dl.find(name);
    if (it != dl.end())
    {
      ::Address address;
      address.set_address(unhex(it->second));
      grpc::ClientContext ctx;
      kv->remove(&ctx, address, &status);
    }
    dl[name] = chb_addr; // play or re-play our operation into dl
    mb.set_data_plain(serialize_document_list(dl));
    {
      grpc::ClientContext ctx;
      ::Update update;
      update.mutable_block()->CopyFrom(mb);
      update.set_decrypt_data(true);
      kv->update(&ctx, update, &status);
    }
    if (!status.has_exception_ptr())
      break; // all good
    if (!status.exception_ptr().exception().has_current())
      throw std::runtime_error(status.exception_ptr().exception().message());
    // try again from the current block version
    mb.CopyFrom(status.exception_ptr().exception().current());
    dl = parse_document_list(mb.data_plain());
  }
}
```

## Complete code ##

<pre><code class="complete lang-python notInFullCode"></code></pre>
<pre><code class="complete lang-go notInFullCode"></code></pre>
<pre><code class="complete cpp notInFullCode"></code></pre>