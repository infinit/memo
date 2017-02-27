The Infinit Key-Value store
===========================

Infinit provides a distributed decentralized key-value store, with built-in
replication and security.

This key-value store is accessible through a [grpc](http://www.grpc.io) interface
specified in the file [kv.proto](kv.proto).

## API overview ##

At its most basic level, a key-value store provides a mapping from an Address to
its corresponding content. The API is comprised of four main methods:

* `Block Get(Address)`: Returns the data Block found at given address.
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

* `Address address`: The address of the Block. This field must be empty when
calling the `insert()` method.
* `string payload`: The data payload contained in the block.

Block contains also one union field whose content depends on the block type.

### Immutable blocks (CHB) ###

The CHB is an immutable block (it cannot be updated) whose address is the hash of
its content. This feature ensures the data is tamper-proof: when fetching a CHB
through the `Get` API call, infinit will recompute the hash of the content and check
it against the address. If both hash do not match `Get` will return a `VALIDATION_FAILED`
error.

CHB exposes one additional feature through the `block.constant_block.owner` field:
if set to the address of an ACB, then removing the CHB will only be allowed if
the user would be allowed to remove said ACB.

<a name="MB"></a>
### Mutable blocks (MB) ###

Mutable blocks are versioned data blocks that can be updated.

The `block.mutable_block` message contains a single field: `int64 version`.
When reading a block through `Get`, this field is set with the current block version.
When writing a block through `Update`, you have two options:

* Set `version` to 0. In which case infinit will simply write the block by
  incrementing the current version by 1. In case of two concurrent updates of the
  same MB they will both succeed but one of them will be lost.
* Set `version` to any non-zero value. In that case the update operation will only
  succeed if the current version is strictly less than that value. Otherwise the
  update will fail with an `ERROR_CONFLICT` message containing the current block
  version.

This second option is needed if you need to make an "atomic" update to a MB:
for instance if your MB's payload is a list of values and you want to add one item
to that list, you need to handle the case where another task is also making an
update to the list at the same time. See the [example](#example) below for an
illustration of this use case.

### ACL blocks (ACB) ###

ACB inherits the feature of Mutable Block and provide additional fields to control
which users can read and write the data.

By default an ACB can only be read and written by the user who created the block.

The `block.acl_block` message of type `ACLBlockData` has the following fields:

* `world_read`: If true all users will be allowed to read the ACB payload. 
* `world_write`: If true all users will be allowed to update the ACB payload.
* `permissions`: A list of `ACL`.

The `ACL` message exposes the following fields:

* `user`: user concerned by this ACL. This can be either a user name (the KV store
keeps a mapping between user names and their public keys), or a serialized public key.
* `read`: give read access to the user.
* `write`: give write access to the user.


When reading an ACB, all ACLs will be returned in the `block.acl_block.permissions` field.
When updating an ACB, ACLs will be *updated* (not replaced) with the content of that field.
As a consequence, if you want to remove access to a given user, you need to set both
`read` and `write` to false in its `permissions` ACL entry.

<a name="NB"></a>
### Named blocks (NB) ###

Named Blocks have the unique feature of being accessible from a user-defined unique-id.
They are typically used as an entry point to access further data, usually by storing
the address of an other Mutable Block in their payload.

NBs are currently Immutable and thus cannot be updated once created.

To insert a NB, simply fill the `block.named_block.name` field with your unique id.
To retrieve a NB, fill the `address` `Get` argument to your unique id, prefixed with *NB:* (no spaces).


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
"name=address\nname2=address2\n...".

### Generating the GRPC and protobuf sources ###

The first step is to generate the sources from the [kv.proto] file. This can
be achieved by the following two commands:

```
$> protoc -I/path --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` /path/kv.proto
$> protoc -I/path --cpp_out=. /path/kv.proto
```

where *path* is the path where *kv.proto* is stored.

This step will generate four files, *kv.pb.h*, *kv.pb.cc*, *kv.grpc.pb.h* and *kv.grpc.pb.cc*.

You need to include *kv.grpc.pb.h* in your source file, and compile the two *.cc* files.

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

#include <kv.grpc.pb.h>
#include <grpc++/grpc++.h>

// Connection to the grpc server
std::shared_ptr<KV::Stub> kv;

using std::vector;
using std::string;
using std::map;

// our API
void create_user(string name);
vector<string> list_documents(string name);
string get_document(string user, string name);
void set_document(string user, string name, string data);

int main(int argc, char** argv)
{
    auto chan = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
    kv = KV::NewStub(chan);
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
void create_user(string name)
{
  // Create and insert an empty mutable block
  ::Block block;
  block.set_address("");
  block.set_payload("");
  block.mutable_mutable_block(); // this sets the block type
  ::Status res;
  {
    grpc::ClientContext ctx;
    kv->Insert(&ctx, block, &res);
  }
  // Create and insert a Named Block with the MB address as payload.
  // We can reuse the block object
  block.mutable_named_block()->set_name(name); // changes block type
  block.set_payload(res.address());
  {
    grpc::ClientContext ctx;
    kv->Insert(&ctx, block, &res);
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
map<string, string> get_documents(string user, int* version = nullptr, string* db_address = nullptr)
{
  ::Address address;
  // Get the NB
  address.set_address("NB:" + user);
  ::BlockStatus res;
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
    if (res.status().error() != ERROR_OK)
      throw std::runtime_error("No such user");
  }
  // Get the document list MB
  address.set_address(res.block().payload());
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
  }
  if (version)
    *version = res.status().version();
  if (db_address)
    *db_address = res.block().address();
  auto dl = parse_document_list(res.block().payload());
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
  address.set_address(it->second);
  ::BlockStatus res;
  {
    grpc::ClientContext ctx;
    kv->Get(&ctx, address, &res);
  }
  return res.block().payload();
}
```

### set_document ###

To create or update a document, we first need to create a new CHB with the document
content, and then add it to the user document list.
Here comes the trickiest part: we need to update the document list atomically, in
case two tasks try to update said document list at the same time. For that we will
set the Mutable Block version number and retry until there is no update conflict.

<div id="code6"></div>
```
void set_document(string user, string name, string data)
{
  int version;
  string dl_address;
  auto dl = get_documents(user, &version, &dl_address);
  // Create and insert the document CHB
  ::Block block;
  block.mutable_constant_block(); // set block type
  block.set_payload(data);
  ::Status status;
  {
    grpc::ClientContext ctx;
    kv->Insert(&ctx, block, &status);
  }
  string chb_addr = status.address();
  while (true)
  {
    // If name is already in the document list, cleanup the data CHB
    auto it = dl.find(name);
    if (it != dl.end())
    {
      ::Address address;
      address.set_address(it->second);
      grpc::ClientContext ctx;
      kv->Remove(&ctx, address, &status);
    }
    dl[name] = chb_addr; // play or re-play our operation into dl
    block.set_address(dl_address);
    block.set_payload(serialize_document_list(dl));
    block.mutable_mutable_block()->set_version(version + 1);
    {
      grpc::ClientContext ctx;
      kv->Update(&ctx, block, &status);
    }
    if (status.error() == ERROR_OK)
      break; // all good
    if (status.error() != ERROR_CONFLICT)
      throw std::runtime_error(status.message());
    // fetch the document list block again
    ::BlockStatus bs;
    {
      ::Address address;
      address.set_address(dl_address);
      grpc::ClientContext ctx;
      kv->Get(&ctx, address, &bs);
    }
    // deserialize the content into dl, it was updated by some other task
    dl = parse_document_list(bs.block().payload());
    // update the version
    version = bs.block().mutable_block().version();
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