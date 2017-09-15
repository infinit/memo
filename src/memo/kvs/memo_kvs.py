#!/usr/bin/env python3

import argparse
import sys

parser = argparse.ArgumentParser('memo')
parser.add_argument('endpoint', help = 'memo gRPC endpoint')
args = parser.parse_args()

import grpc
from memo_kvs_pb2_grpc import KeyValueStoreStub as KeyValueStore
from memo_kvs_pb2 import *

channel = grpc.insecure_channel(args.endpoint)
store = KeyValueStore(channel)

def delete(key):
  store.Delete(DeleteRequest(key = key))

def fetch(key):
  return store.Fetch(FetchRequest(key = key)).value.decode('utf-8')

def insert(key, value):
  store.Insert(InsertRequest(key = key, value = value.encode('utf-8')))

def list_(**kwargs):
  return list(map(lambda i: i.key, store.List(ListRequest(**kwargs)).items))

def update(key, value):
  return store.Update(UpdateRequest(key = key, value = value.encode('utf-8')))

def upsert(key, value):
  return store.Upsert(UpsertRequest(key = key, value = value.encode('utf-8')))
