#!/usr/bin/env python3

import grpc
import service_pb2_grpc
import service_pb2 as kv
import sys

def insert(key, value):
  stub.insert(kv.InsertRequest(key = key, value = value.encode('utf-8')))

def update(key, value):
  stub.update(kv.UpdateRequest(key = key, value = value.encode('utf-8')))

def upsert(key, value):
  stub.upsert(kv.UpsertRequest(key = key, value = value.encode('utf-8')))

def get(key):
  return stub.get(kv.GetRequest(key = key)).value.decode('utf-8')

def remove(key):
  stub.remove(kv.RemoveRequest(key = key))

def list_():
  return list(map(lambda i: i.key, stub.list(kv.ListRequest()).items))

if __name__ == "__main__":
  channel = grpc.insecure_channel(sys.argv[1])
  stub = service_pb2_grpc.kvStub(channel)

  command = sys.argv[2]
  if command == 'insert':
    insert(sys.argv[3], sys.argv[4])
  elif command == 'update':
    update(sys.argv[3], sys.argv[4])
  elif command == 'upsert':
    upsert(sys.argv[3], sys.argv[4])
  elif command == 'get':
    print(get(sys.argv[3]))
  elif command == 'remove':
    remove(sys.argv[3])
  elif command == 'list':
    print(list_())
  else:
    raise Exception("unknown command")
