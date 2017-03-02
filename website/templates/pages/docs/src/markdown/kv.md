The Infinit Key-Value store
===========================

Infinit provides a distributed decentralized key-value store, with built-in
replication and security.

This key-value store is accessible through a [grpc](http://www.grpc.io) interface
specified in the file [kv.proto](kv.proto).

## API overview ##

At its most basic level, a key-value store provides a mapping from an Address to
its corresponding content. The API is comprised of four main methods:

* `BlockOrStatus Get(Address)`: Returns the data Block found at given address or an error.
* `Status Remove(Address)`: Erase the data Block at given address.
* `Address Insert(Block)`: Insert a new block into the KV store, and return
  the address that was attributed to it.
* `Status Update(Block)`: Update a data block without changing its address.

One thing of importance should be noted at this point: in the infinit KV-store
one cannot chose the address that will be allocated to a given block: the
address is allocated by infinit in a way that prevents unauthorized users
or compromised storage nodes from tampering with the content.

## Block types ##

The infinit KV store supports many different block types that serve different
purposes. The most important ones are:

* Constant Hash Block(CHB): The simplest of all blocks, the CHB is an immutable
  block whose address is simply the hash of its content.
* Mutable Block (MB): Basic mutable block, providing atomic update (read-then-write)
  using a versioning mechanism.
* Address Control List Block (ACB): Refinement over mutable blocks providing a
  fined-grained ACL system which can be used to defined which users can read and
  write the block content.
* Named Block (NB): Blocks whose address can be deduced from their unique name.
  They typically store the address of another MB as their payload.

### The Block message type ###

Blocks are read and written on the KV store through the `Block` protobuf message
type. Block contains two fields common to all block types:

* `bytes address`: The address of the Block.
* `bytes data`: The data payload contained in the block.

Block contains more field whose meaning depends on the block type.

### Immutable blocks (CHB) ###

The CHB is an immutable block (it cannot be updated) whose address is the hash of
its content. This feature ensures the data is tamper-proof: when fetching a CHB
through the `Get` API call, infinit will recompute the hash of the content and check
it against the address. If both hash do not match `Get` will return a `VALIDATION_FAILED`
error.

CHB exposes one additional feature through the `block.owner` field:
if set to the address of an ACB, then removing the CHB will only be allowed if
the user would be allowed to remove said ACB.

<a name="MB"></a>
### Mutable blocks (MB) ###

Mutable blocks are versioned data blocks that can be updated.

Infinit enforces an atomic update scheme by using a versioning mechanism:
When you `Get` a mutable block, the returned `Block` contains a version field
that contains the block version at the moment the read occured.
When you attempt to call `Update`, that version gets incremented by one, and
the update will only succeed if the resulting number is above the current
block version. Otheriwise the `Update` call will fail with a CONFLICT error
message.

Atomic updates is an important feature. For instance if your MB's payload is
a list of values and you want to add one item
to that list, you need to handle the case where another task is also making an
update to the list at the same time. See the [example](#example) below for an
illustration of this use case.

### ACL blocks (ACB) ###

ACB inherits the feature of Mutable Block and provide additional fields to control
which users can read and write the data.

By default an ACB can only be read and written by the user who created the block.

The `Block` message of type `ACLEntry` has the following fields used to control ACLs:

* `world_read`: If true all users will be allowed to read the ACB payload. 
* `world_write`: If true all users will be allowed to update the ACB payload.
* `acl`: A list of `ACLEntry`.

The `ACLEntry` message exposes the following fields:

* `key_koh`: user concerned by this ACL. This field containes the serialized public key of the user.
* `read`: give read access to the user.
* `write`: give write access to the user.

<a name="NB"></a>
### Named blocks (NB) ###

Named Blocks have the unique feature of being accessible from a user-defined unique-id.
They are typically used as an entry point to access further data, usually by storing
the address of an other Mutable Block in their payload.

NBs are currently Immutable and thus cannot be updated once created.
Use the `NBAddress(Bytes)` function to obtain the NB block address from its unique id.

## Creating and inserting new blocks ##

When inserting new blocks into the key-value store, one first needs to obtain
a `Block` message through one of the builder functions:

* Block MakeCHB(CHBData) : create CHB with given payload and owner
* Block MakeOKB(Empty) : create OKB (no arguments)
* Block MakeACB(Empty) : create ACB (no arguments)
* Block MakeNB(Bytes) : create NB with given key

You can then fill the `data` field with your payload (for mutable blocks) and
call the `Insert` function.

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
* For each document, one [CHB](#CHB) will store the document content.

For the document list format, we will use a simple text serialization scheme of the form
"name=address\nname2=address2\n...". We will encode addresses in hexadecimal.

### Generating the GRPC and protobuf sources ###

The first step is to generate the sources from the [kv.proto] file. This can
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
    auto chan = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
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
  // Create and insert an empty mutable block
  ::Block doclist;
  ::Empty empty;
  {
     grpc::ClientContext ctx;
     kv->MakeOKB(&ctx, empty, &doclist);
  }
  {
    grpc::ClientContext ctx;
    ::Status res;
    kv->Insert(&ctx, doclist, &res);
  }
  // Create and insert a Named Block with the MB address as payload.
  // We can reuse the block object
  ::Bytes key;
  ::Block nb;
  key.set_data(user);
  {
    grpc::ClientContext ctx;
    kv->MakeNB(&ctx, key, &nb);
  }
  nb.set_data(doclist.address());
  {
    ::Status res;
    grpc::ClientContext ctx;
    kv->Insert(&ctx, nb, &res);
    if (res.error() != ERROR_OK)
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
  ::Bytes key;
  key.set_data(user);
  // Get the NB address
  {
    grpc::ClientContext ctx;
    kv->NBAddress(&ctx, key, &address);
  }
  // Get the NB
  ::BlockOrStatus res;
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
    if (res.has_status())
      throw std::runtime_error("No such user (" + res.status().message() + ")");
  }
  // Get the document list MB
  address.set_address(res.block().data());
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
  }
  if (block)
    block->CopyFrom(res.block());
  auto dl = parse_document_list(res.block().data());
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
extract the document address, and fetch its CHB

<div id="code5"></div>
```
string get_document(string user, string name)
{
  auto dl = get_documents(user);
  auto it = dl.find(name);
  if (it == dl.end())
    throw std::runtime_error("no such document");
  ::Address address;
  address.set_address(unhex(it->second));
  ::BlockOrStatus res;
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
  }
  return res.block().data();
}
```

### set_document ###

To create or update a document, we first need to create a new CHB with the document
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
  // Create and insert the document CHB
  ::CHBData cdata;
  cdata.set_data(data);
  ::Block doc;
  {
    grpc::ClientContext ctx;
    kv->MakeCHB(&ctx, cdata, &doc);
  }
  ::Status status;
  {
    grpc::ClientContext ctx;
    kv->Insert(&ctx, doc, &status);
  }
  string chb_addr = hex(doc.address());
  while (true)
  {
    // If name is already in the document list, cleanup the data CHB
    auto it = dl.find(name);
    if (it != dl.end())
    {
      ::Address address;
      address.set_address(unhex(it->second));
      grpc::ClientContext ctx;
      kv->Remove(&ctx, address, &status);
    }
    dl[name] = chb_addr; // play or re-play our operation into dl
    mb.set_data(serialize_document_list(dl));
    {
      grpc::ClientContext ctx;
      kv->Update(&ctx, mb, &status);
    }
    if (status.error() == ERROR_OK)
      break; // all good
    if (status.error() != ERROR_CONFLICT)
      throw std::runtime_error(status.message());
    // fetch the document list block again to get updated content and version
    dl = get_documents(user, &mb);
    // try again
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