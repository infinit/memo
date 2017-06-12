package main

import (
  "fmt"
  "golang.org/x/net/context"
  "google.golang.org/grpc"
  "google.golang.org/grpc/grpclog"
  kv "kv"
  "os"
)

func main() {
  conn, err := grpc.Dial(os.Args[1], grpc.WithInsecure())
  if err != nil {
    grpclog.Fatalf("failed to dial: %v", err)
  }
  defer conn.Close()
  client := kv.NewKvClient(conn)
  cmd := os.Args[2]
  switch cmd {
  default:
    grpclog.Fatalf("unknown command: %v", cmd)
  case "insert":
    insert(client, os.Args[3], os.Args[4])
  case "update":
    update(client, os.Args[3], os.Args[4])
  case "upsert":
    upsert(client, os.Args[3], os.Args[4])
  case "get":
    value, err := get(client, os.Args[3])
    if err != nil {
      grpclog.Fatalf("unable to get: %v", err)
    }
    fmt.Printf("%s\n", value)
  case "remove":
    remove(client, os.Args[3])
  case "list":
    list, err := list(client)
    if err != nil {
      grpclog.Fatalf("unable to list: %v", err)
    }
    fmt.Printf("%v\n", list)
  }
}

func insert(client kv.KvClient, key string, value string) error {
  _, err := client.Insert(context.Background(), &kv.InsertRequest{Key: key, Value: []byte(value)})
  return err
}

func update(client kv.KvClient, key string, value string) error {
  _, err := client.Update(context.Background(), &kv.UpdateRequest{Key: key, Value: []byte(value)})
  return err
}

func upsert(client kv.KvClient, key string, value string) error {
  _, err := client.Upsert(context.Background(), &kv.UpsertRequest{Key: key, Value: []byte(value)})
  return err
}

func get(client kv.KvClient, key string) (string, error) {
  res, err := client.Get(context.Background(), &kv.GetRequest{Key: key})
  if err != nil {
    return "", err
  }
  return string(res.Value), nil
}

func remove(client kv.KvClient, key string) error {
  _, err := client.Remove(context.Background(), &kv.RemoveRequest{Key: key})
  return err
}

func list(client kv.KvClient) ([]string, error) {
  res, err := client.List(context.Background(), &kv.ListRequest{})
  if err != nil {
    return nil, err
  }
  result := []string{}
  for _, i := range res.Items {
    result = append(result, i.Key)
  }
  return result, nil
}
