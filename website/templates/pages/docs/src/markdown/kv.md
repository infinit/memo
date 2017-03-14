The Infinit Key-Value store
===========================

Infinit provides a distributed decentralized key-value store, with built-in
replication and security.

This key-value store is accessible through a [grpc](http://www.grpc.io) interface
specified in the file [doughnut.proto](doughnut.proto).

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

* Immutable blocks (IB): The simplest of all blocks, whose address is simply the hash of their content.
* Mutable Block (MB): Basic mutable block, providing atomic update (read-then-write)
  using a versioning mechanism, and security since the payload is encrypted.
* Address Control List Block (ACLB): Refinement over mutable blocks providing a
  fined-grained ACL system which can be used to defined which users can read and
  write the block content.
* Named Block (NB): Blocks whose address can be deduced from their unique name.
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

<a name="MB"></a>
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

<a name="NB"></a>
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

## Example: a simple multi-user document storage system ##

This section provides a complete example of a simple use case of the infinit KV store.

Our task for this project is to provide a simple document storage system for multiple users.

### The public API ###

Our goal is to implement the following simple API:

```
  void create_user(string user);
  vector<string> list_documents(string user);
  string get_document(string user, string name);
  void set_document(string user, string name, string data);
```

### specifying our block layout ###

We will use the following blocks:

* For each user, one [NB](#NB) keyed with the user name will contain the address
  of the document list block.
* For each user, one [MB](#MB) will serve as the document list. Its payload will
  be a serialized map of document names to the document content address.
* For each document, one [IB](#IB) will store the document content.

For the document list format, we will use a simple text serialization scheme of the form
"name=address\nname2=address2\n...". We will encode addresses in hexadecimal.

### Generating the GRPC and protobuf sources ###

The first step is to generate the sources from the [doughnut.proto] file. This can
be achieved by the following two commands:

```
$> protoc -I/path --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` /path/doughnut.proto
$> protoc -I/path --cpp_out=. /path/doughnut.proto
```

where *path* is the path where *kv.proto* is stored.

This step will generate four files, *doughnut.pb.h*, *doughnut.pb.cc*, *doughnut.grpc.pb.h* and *doughnut.grpc.pb.cc*.

You need to include *doughnut.grpc.pb.h* in your source file, and compile the two *.cc* files.

### Connecting to the grpc KV store server ###

Let us start with the boilerplate needed to connect to the grpc server. We will
accept the grpc endpoint name into the first command line argument.
We also take a command from *create*, *list*, *get*, *set* and its arguments from the
command line.

<div id="code1"></div>
```
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


<div id="code2"></div>
```
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

<div id="code3"></div>
```
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

<div id="code4"></div>
```
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

<div id="code5"></div>
```
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

<div id="code6"></div>
```
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
      kv->update(&ctx, update, &status);
    }
    if (!status.has_exception_ptr())
      break; // all good
    if (!status.exception_ptr().exception().has_current())
      throw std::runtime_error(status.exception_ptr().exception().message());
    // try again from the current block version
    mb.CopyFrom(status.exception_ptr().exception().current());
    dl = parse_document_list(mb.data());
  }
}
```

### the complete code ###

<pre><code><div id="complete"></div></code></pre>

<script language="javascript">
document.getElementById("complete").innerHTML = (""
    + document.getElementById("code1").nextSibling.nextSibling.nextSibling.innerHTML
    + document.getElementById("code2").nextSibling.nextSibling.nextSibling.innerHTML
    + document.getElementById("code3").nextSibling.nextSibling.nextSibling.innerHTML
    + document.getElementById("code4").nextSibling.nextSibling.nextSibling.innerHTML
    + document.getElementById("code5").nextSibling.nextSibling.nextSibling.innerHTML
    + document.getElementById("code6").nextSibling.nextSibling.nextSibling.innerHTML);
</script>