package main

import (
  "fmt"
  "golang.org/x/net/context"
  "google.golang.org/grpc"
  "google.golang.org/grpc/grpclog"
  kvs "kvs"
  "os"
)

func main() {
  conn, err := grpc.Dial(os.Args[1], grpc.WithInsecure())
  if err != nil {
    grpclog.Fatalf("failed to dial: %v", err)
  }
  defer conn.Close()
  store := kvs.NewKeyValueStoreClient(conn)
  cmd := os.Args[2]
  switch cmd {
  default:
    grpclog.Fatalf("unknown command: %v", cmd)
  case "insert":
    insert(store, os.Args[3], os.Args[4])
  case "update":
    update(store, os.Args[3], os.Args[4])
  case "upsert":
    upsert(store, os.Args[3], os.Args[4])
  case "fetch":
    value, err := fetch(store, os.Args[3])
    if err != nil {
      grpclog.Fatalf("unable to get: %v", err)
    }
    fmt.Printf("%s\n", value)
  case "delete":
    delete_(store, os.Args[3])
  case "list":
    list, err := list(store)
    if err != nil {
      grpclog.Fatalf("unable to list: %v", err)
    }
    fmt.Printf("%v\n", list)
  }
}

func insert(store kvs.KeyValueStoreClient, key string, value string) error {
  _, err := store.Insert(context.Background(), &kvs.InsertRequest{Key: key, Value: []byte(value)})
  return err
}

func update(store kvs.KeyValueStoreClient, key string, value string) error {
  _, err := store.Update(context.Background(), &kvs.UpdateRequest{Key: key, Value: []byte(value)})
  return err
}

func upsert(store kvs.KeyValueStoreClient, key string, value string) error {
  _, err := store.Upsert(context.Background(), &kvs.UpsertRequest{Key: key, Value: []byte(value)})
  return err
}

func fetch(store kvs.KeyValueStoreClient, key string) (string, error) {
  res, err := store.Fetch(context.Background(), &kvs.FetchRequest{Key: key})
  if err != nil {
    return "", err
  }
  return string(res.Value), nil
}

func delete_(store kvs.KeyValueStoreClient, key string) error {
  _, err := store.Delete(context.Background(), &kvs.DeleteRequest{Key: key})
  return err
}

func list(store kvs.KeyValueStoreClient) ([]string, error) {
  res, err := store.List(context.Background(), &kvs.ListRequest{})
  if err != nil {
    return nil, err
  }
  result := []string{}
  for _, i := range res.Items {
    result = append(result, i.Key)
  }
  return result, nil
}
