#!/usr/bin/env python3

import grpc
import memo_kvs_pb2_grpc
import memo_kvs_pb2 as kvs
import sys

if __name__ == "__main__":
  channel = grpc.insecure_channel(sys.argv[1])
  store = memo_kvs_pb2_grpc.KeyValueStoreStub(channel)

  def insert(key, value):
    store.Insert(kvs.InsertRequest(key = key, value = value.encode('utf-8')))

  def update(key, value):
    store.Update(kvs.UpdateRequest(key = key, value = value.encode('utf-8')))

  def upsert(key, value):
    store.Upsert(kvs.UpsertRequest(key = key, value = value.encode('utf-8')))

  def fetch(key):
    return store.Fetch(kvs.FetchRequest(key = key)).value.decode('utf-8')

  def delete(key):
    store.Delete(kvs.DeleteRequest(key = key))

  def list_():
    return list(map(lambda i: i.key, store.List(kvs.ListRequest()).items))

  command = sys.argv[2]
  if command == 'insert':
    insert(sys.argv[3], sys.argv[4])
  elif command == 'update':
    update(sys.argv[3], sys.argv[4])
  elif command == 'upsert':
    upsert(sys.argv[3], sys.argv[4])
  elif command == 'fetch':
    print(fetch(sys.argv[3]))
  elif command == 'delete':
    delete(sys.argv[3])
  elif command == 'list':
    print(list_())
  else:
    raise Exception("unknown command")
