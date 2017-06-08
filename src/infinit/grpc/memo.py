#!/usr/bin/env python3

import argparse
import sys

parser = argparse.ArgumentParser('memo')
parser.add_argument('endpoint', help = 'memo gRPC endpoint')
args = parser.parse_args()

import grpc
import memo_pb2_grpc
from memo_pb2 import *

channel = grpc.insecure_channel(args.endpoint)
kv = memo_pb2_grpc.ValueStoreStub(channel)
