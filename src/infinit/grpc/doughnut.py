#!/usr/bin/env python3

import argparse
import sys

parser = argparse.ArgumentParser('doughnut')
parser.add_argument('endpoint', help = 'Doughnut GRPC endpoint')
args = parser.parse_args()

import grpc
import doughnut_pb2_grpc
from doughnut_pb2 import *

channel = grpc.insecure_channel(args.endpoint)
dht = doughnut_pb2_grpc.DoughnutStub(channel)
