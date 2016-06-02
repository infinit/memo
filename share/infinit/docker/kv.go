package main

import (
  "fmt"
//	"sort"
  "strings"
	"os"
	"github.com/docker/libkv"
	"github.com/docker/libkv/store"
	"github.com/docker/libkv/store/zookeeper"
	"github.com/docker/libkv/store/consul"
	"github.com/docker/libkv/store/etcd"
)

func check(err error) {
  if (err != nil) {
    fmt.Println(err)
    os.Exit(1)
  }
}
func main() {
	zookeeper.Register()
	consul.Register()
	etcd.Register()
  url := os.Args[1]
  components := strings.Split(url, "://")
  backend_name := components[0]
  url = components[1]
  backend := store.ZK
  if (backend_name == "consul") {
    backend = store.CONSUL
  }
  if (backend_name == "etcd") {
    backend = store.ETCD
  }
  kv,err := libkv.NewStore(backend, []string{url}, &store.Config{})
  check(err)
  op := os.Args[2]
  if (op == "get") {
    val, err := kv.Get(os.Args[3])
    check(err)
    fmt.Println(string(val.Value))
  }
  if (op == "list") {
    ents, err := kv.List(os.Args[3])
    check(err)
    for _, pair := range ents {
      fmt.Printf("%v: %v\n", pair.Key, string(pair.Value))
    }
  }
  if (op == "set") {
    err = kv.Put(os.Args[3], []byte(os.Args[4]), nil)
    check(err)
  }
  if (op == "delete") {
    err = kv.Delete(os.Args[3])
    check(err)
  }
  if (op == "watchtree") {
    key := os.Args[3]
    res, err := kv.Exists(key)
    if !res {
      err := kv.Put(key, []byte(""), &store.WriteOptions{IsDir:true})
      check(err)
    }
    stopCh := make(<-chan struct{})
    events, err := kv.WatchTree(key, stopCh)
    check(err)
    for true {
      select {
        case pairs := <-events:
          for _, pair := range pairs {
            fmt.Printf("%v: %v\n", pair.Key, string(pair.Value))
          }
      }
    }
  }
}
