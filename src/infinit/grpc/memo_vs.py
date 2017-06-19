#!/usr/bin/env python3

import argparse
import sys

parser = argparse.ArgumentParser('memo')
parser.add_argument('endpoint', help = 'memo gRPC endpoint')
args = parser.parse_args()

import grpc
from memo_vs_pb2_grpc import ValueStoreStub as ValueStore
from memo_vs_pb2 import *

channel = grpc.insecure_channel(args.endpoint)
kv = ValueStore(channel)
