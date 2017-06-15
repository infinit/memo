#include <memo_kvs.grpc.pb.h>
#include <grpc++/grpc++.h>

using namespace memo::kvs;

std::unique_ptr<KeyValueStore::Stub> store;

void
check_status(::grpc::Status const& status)
{
  if (!status.ok())
  {
    std::cerr << status.error_message() << std::endl;
    exit(1);
  }
}

void
insert(std::string const& key, std::string const& value)
{
  InsertResponse res;
  {
    grpc::ClientContext ctx;
    InsertRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = store->Insert(&ctx, req, &res);
    check_status(status);
  }
}

void
update(std::string const& key, std::string const& value)
{
  UpdateResponse res;
  {
    grpc::ClientContext ctx;
    UpdateRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = store->Update(&ctx, req, &res);
    check_status(status);
  }
}

void
upsert(std::string const& key, std::string const& value)
{
  UpsertResponse res;
  {
    grpc::ClientContext ctx;
    UpsertRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = store->Upsert(&ctx, req, &res);
    check_status(status);
  }
}

std::string
fetch(std::string const& key)
{
  FetchResponse res;
  {
    grpc::ClientContext ctx;
    FetchRequest req;
    req.set_key(key);
    auto status = store->Fetch(&ctx, req, &res);
    check_status(status);
  }
  return res.value();
}

void
delete_(std::string const& key)
{
  DeleteResponse res;
  {
    grpc::ClientContext ctx;
    DeleteRequest req;
    req.set_key(key);
    auto status = store->Delete(&ctx, req, &res);
    check_status(status);
  }
}

std::vector<std::string>
list()
{
  ListResponse res;
  {
    grpc::ClientContext ctx;
    ListRequest req;
    auto status = store->List(&ctx, req, &res);
  }
  std::vector<std::string> list;
  for (int i = 0; i < res.items_size(); i++)
    list.push_back(res.items(i).key());
  return list;
}

int
main(int argc, char** argv)
{
  auto chan = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
  store = KeyValueStore::NewStub(chan);
  std::string cmd = argv[2];
  if (cmd == "insert")
    insert(argv[3], argv[4]);
  else if (cmd == "update")
    update(argv[3], argv[4]);
  else if (cmd == "upsert")
    upsert(argv[3], argv[4]);
  else if (cmd == "fetch")
    std::cout << fetch(argv[3]) << std::endl;
  else if (cmd == "delete")
    delete_(argv[3]);
  else if (cmd == "list")
  {
    auto l = list();
    std::cout << "[";
    for (int i = 0; i < l.size(); i++)
      std::cout << l[i] << (i < l.size() - 1 ? ", " : "");
    std::cout << "]" << std::endl;
  }
  else
    std::cerr << "unknown command: " << argv[2] << std::endl;
  return 0;
}
