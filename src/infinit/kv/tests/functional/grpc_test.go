package main

import (
	"fmt"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"io/ioutil"
	kv "kv/service"
	"math/rand"
	"os"
	"os/exec"
	"reflect"
	"runtime"
	"sort"
	"sync"
	"testing"
	"time"
)

func TestSimple(t *testing.T) {
	var err error
	i := NewInfrastructure(t)
	defer i.CleanUp()
	key := "some/key"
	value1 := "some data"

	// Fetch value that doesn't exist.
	_, err = i.Client.Get(context.Background(), &kv.GetRequest{Key: key})
	AssertEq(t, grpc.Code(err), codes.NotFound)

	// Remove value that doesn't exist.
	_, err = i.Client.Remove(context.Background(), &kv.RemoveRequest{Key: key})
	AssertEq(t, grpc.Code(err), codes.NotFound)

	// Update value that doesn't exist.
	_, err = i.Client.Update(context.Background(),
		&kv.UpdateRequest{Key: key, Value: []byte("update")})
	AssertEq(t, grpc.Code(err), codes.NotFound)

	// Insert value that doesn't exist.
	_, err = i.Client.Insert(context.Background(),
		&kv.InsertRequest{Key: key, Value: []byte(value1)})
	AssertEq(t, err, nil)

	// Fetch value.
	getRes, _ := i.Client.Get(context.Background(), &kv.GetRequest{Key: key})
	AssertEq(t, getRes.GetValue(), []byte(value1))

	// Insert value that already exists.
	_, err = i.Client.Insert(context.Background(), &kv.InsertRequest{Key: key, Value: []byte("exists")})
	AssertEq(t, grpc.Code(err), codes.AlreadyExists)

	// Update value.
	value2 := "update data"
	i.Client.Update(context.Background(), &kv.UpdateRequest{Key: key, Value: []byte(value2)})
	getRes, _ = i.Client.Get(context.Background(), &kv.GetRequest{Key: key})
	AssertEq(t, getRes.GetValue(), []byte(value2))

	// Upsert value.
	value3 := "upsert data"
	i.Client.Upsert(context.Background(), &kv.UpsertRequest{Key: key, Value: []byte(value3)})
	getRes, _ = i.Client.Get(context.Background(), &kv.GetRequest{Key: key})
	AssertEq(t, getRes.GetValue(), []byte(value3))

	// Remove value.
	_, err = i.Client.Remove(context.Background(), &kv.RemoveRequest{Key: key})
	AssertEq(t, err, nil)
	_, err = i.Client.Get(context.Background(), &kv.GetRequest{Key: key})
	AssertEq(t, grpc.Code(err), codes.NotFound)
}

func TestList(t *testing.T) {
	i := NewInfrastructure(t)
	defer i.CleanUp()

	// Store test data.
	keys := []string{}
	data := map[string][]byte{}
	for j := 0; j < 100; j++ {
		prefix := "dir_1/"
		if j >= 50 {
			prefix = "dir_2/"
		}
		if j%2 == 0 {
			prefix += "a"
		} else {
			prefix += "b"
		}
		k := fmt.Sprintf("%s/%d", prefix, j)
		keys = append(keys, k)
		v := make([]byte, 100)
		rand.Read(v)
		data[k] = v
		_, err := i.Client.Insert(context.Background(),
			&kv.InsertRequest{Key: k, Value: v})
		AssertEq(t, err, nil)
	}
	sort.Strings(keys)

	// Check test data.
	for _, k := range keys {
		res, _ := i.Client.Get(context.Background(), &kv.GetRequest{Key: k})
		AssertEq(t, res.GetValue(), data[k])
	}

	// All.
	list, _ := i.Client.List(context.Background(), &kv.ListRequest{})
	AssertEq(t, len(list.GetItems()), 100)
	for j := 0; j < len(keys); j++ {
		AssertEq(t, list.GetItems()[j].GetKey(), keys[j])
	}

	// 10.
	list, _ = i.Client.List(context.Background(), &kv.ListRequest{MaxKeys: 10})
	AssertEq(t, len(list.GetItems()), 10)
	AssertEq(t, list.GetTruncated(), true)
	for j := 0; j < len(list.GetItems()); j++ {
		AssertEq(t, list.GetItems()[j].GetKey(), keys[j])
	}

	// Next 10.
	marker := list.GetItems()[len(list.GetItems())-1].GetKey()
	list, _ = i.Client.List(context.Background(),
		&kv.ListRequest{MaxKeys: 10, Marker: marker})
	AssertEq(t, len(list.GetItems()), 10)
	AssertEq(t, list.GetTruncated(), true)
	for j := 0; j < 10; j++ {
		AssertEq(t, list.GetItems()[j].GetKey(), keys[j+10])
	}

	// Prefix.
	list, _ = i.Client.List(context.Background(),
		&kv.ListRequest{Prefix: "dir_1"})
	AssertEq(t, len(list.GetItems()), 50)
	AssertEq(t, list.GetTruncated(), false)

	// Prefix 10.
	list, _ = i.Client.List(context.Background(),
		&kv.ListRequest{Prefix: "dir_2", MaxKeys: 10})
	AssertEq(t, len(list.GetItems()), 10)
	AssertEq(t, list.GetTruncated(), true)

	// Prefix 50.
	list, _ = i.Client.List(context.Background(),
		&kv.ListRequest{Prefix: "dir_1", MaxKeys: 50})
	AssertEq(t, len(list.GetItems()), 50)
	AssertEq(t, list.GetTruncated(), false)

	// Delimiter.
	list, _ = i.Client.List(context.Background(), &kv.ListRequest{Delimiter: "/"})
	AssertEq(t, len(list.GetItems()), 100)
	AssertEq(t, list.GetPrefixes(), []string{"dir_1/", "dir_2/"})

	// Delimiter prefix.
	list, _ = i.Client.List(context.Background(),
		&kv.ListRequest{Delimiter: "/", Prefix: "dir_1/"})
	AssertEq(t, len(list.GetItems()), 50)
	AssertEq(t, list.GetPrefixes(), []string{"a/", "b/"})
}

func TestConcurrent(t *testing.T) {
	i := NewInfrastructure(t)
	defer i.CleanUp()

	data := map[string][]byte{}
	for j := 0; j < 10; j++ {
		v := make([]byte, 2*1024*1024)
		rand.Read(v)
		data[fmt.Sprintf("%d", j)] = v
	}

	wg := sync.WaitGroup{}

	putData := func(k string) {
		defer wg.Done()
		_, err := i.Client.Insert(context.Background(),
			&kv.InsertRequest{Key: k, Value: data[k]})
		AssertEq(t, err, nil)
	}

	wg.Add(len(data))
	for k := range data {
		go putData(k)
	}
	wg.Wait()

	for k, v := range data {
		getRes, _ := i.Client.Get(context.Background(), &kv.GetRequest{Key: k})
		AssertEq(t, getRes.GetValue(), v)
	}
}

func AssertEq(t *testing.T, a interface{}, b interface{}) {
	if !reflect.DeepEqual(a, b) {
		_, f, l, _ := runtime.Caller(1)
		t.Errorf("assert failed at %v:%v : %v != %b", f, l, a, b)
	}
}

type Infrastructure struct {
	Client kv.KvClient
	conn   *grpc.ClientConn
	kvs    *exec.Cmd
	dht    *exec.Cmd
	dir    string
}

func (i *Infrastructure) CleanUp() {
	i.conn.Close()
	i.kvs.Process.Kill()
	i.dht.Process.Kill()
	os.RemoveAll(i.dir)
}

func cmdWithEnv(cmd string, args []string, envMap map[string]string) *exec.Cmd {
	res := exec.Command(cmd, args...)
	var env []string
	for k, v := range envMap {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	res.Env = env
	res.Stdout = os.Stdout
	res.Stderr = os.Stderr
	return res
}

func runInfinitCmds(infinit string, env map[string]string) {
	cmdWithEnv(infinit, []string{"user", "create"}, env).Run()
	cmdWithEnv(infinit, []string{"silo", "create", "filesystem", "s"}, env).Run()
	cmdWithEnv(infinit, []string{"network", "create", "n", "-S", "s", "--protocol", "tcp"}, env).Run()
}

func NewInfrastructure(t *testing.T) *Infrastructure {
	var err error
	res := Infrastructure{}
	defer func() {
		if err != nil {
			res.CleanUp()
		}
	}()
	infinit := os.Getenv("INFINIT_BIN")
	if infinit == "" {
		t.Errorf("INFINIT_BIN not set")
	}
	kvServer := os.Getenv("INFINIT_KV_BIN")
	if kvServer == "" {
		t.Errorf("INFINIT_BIN not set")
	}
	dir, err := ioutil.TempDir("", "grpc")
	if err != nil {
		t.Errorf("unable to create temp dir", err)
	}
	envInfinit := map[string]string{
		"INFINIT_HOME": dir,
		"INFINIT_USER": "grpc",
	}
	runInfinitCmds(infinit, envInfinit)
	rand.Seed(time.Now().UnixNano())
	dhtEp := fmt.Sprintf("127.0.0.1:%d", 50000+rand.Intn(999))
	portFile := fmt.Sprintf("%s/port", dir)
	res.dht = cmdWithEnv(infinit, []string{"network", "run", "n", "--grpc", dhtEp, "--port-file", portFile}, envInfinit)
	res.dht.Start()
	for {
		time.Sleep(100 * time.Millisecond)
		if _, err := os.Stat(portFile); !os.IsNotExist(err) {
			break
		}
	}
	grpcEp := fmt.Sprintf("127.0.0.1:%d", 51000+rand.Intn(999))
	res.kvs = cmdWithEnv(kvServer, []string{"--name", "n", "--bootstrap", "--value-store", dhtEp, "--grpc", grpcEp}, nil)
	res.kvs.Start()
	for {
		time.Sleep(100 * time.Millisecond)
		if res.conn, err = grpc.Dial(grpcEp, grpc.WithInsecure()); err == nil {
			break
		}
	}
	res.Client = kv.NewKvClient(res.conn)
	return &res
}
