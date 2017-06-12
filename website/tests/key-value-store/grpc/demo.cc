#include <service.grpc.pb.h>
#include <grpc++/grpc++.h>

std::unique_ptr<kv::service::kv::Stub> key_value_store;

void
insert(std::string const& key, std::string const& value);
void
update(std::string const& key, std::string const& value);
void
upsert(std::string const& key, std::string const& value);
std::string
get(std::string const& key);
void
remove_(std::string const& key);
std::vector<std::string>
list();

int
main(int argc, char** argv)
{
  auto chan = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
  key_value_store = kv::service::kv::NewStub(chan);
  std::string cmd = argv[2];
  if (cmd == "insert")
    insert(argv[3], argv[4]);
  else if (cmd == "update")
    update(argv[3], argv[4]);
  else if (cmd == "upsert")
    upsert(argv[3], argv[4]);
  else if (cmd == "get")
    std::cout << get(argv[3]) << std::endl;
  else if (cmd == "remove")
    remove_(argv[3]);
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
  kv::service::InsertResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::InsertRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = key_value_store->insert(&ctx, req, &res);
    check_status(status);
  }
}

void
update(std::string const& key, std::string const& value)
{
  kv::service::UpdateResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::UpdateRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = key_value_store->update(&ctx, req, &res);
    check_status(status);
  }
}

void
upsert(std::string const& key, std::string const& value)
{
  kv::service::UpsertResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::UpsertRequest req;
    req.set_key(key);
    req.set_value(value);
    auto status = key_value_store->upsert(&ctx, req, &res);
    check_status(status);
  }
}

std::string
get(std::string const& key)
{
  kv::service::GetResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::GetRequest req;
    req.set_key(key);
    auto status = key_value_store->get(&ctx, req, &res);
    check_status(status);
  }
  return res.value();
}

void
remove_(std::string const& key)
{
  kv::service::RemoveResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::RemoveRequest req;
    req.set_key(key);
    auto status = key_value_store->remove(&ctx, req, &res);
    check_status(status);
  }
}

std::vector<std::string>
list()
{
  kv::service::ListResponse res;
  {
    grpc::ClientContext ctx;
    kv::service::ListRequest req;
    auto status = key_value_store->list(&ctx, req, &res);
  }
  std::vector<std::string> list;
  for (int i = 0; i < res.items_size(); i++)
    list.push_back(res.items(i).key());
  return list;
}

