#! /usr/bin/env python3
import sys
import uuid
import hashlib
import json

if len(sys.argv) < 4:
  print('usage: jsonupdate key value files...\n  Update one key in many json files')
  sys.exit(0)
key = sys.argv[1]
val = sys.argv[2]
try:
  val = int(val)
except:
  pass
key = key.split('.')
for f in sys.argv[3:]:
  with open(f, "r") as fj:
    content = json.load(fj)
    node = content
    for k in key[0:-1]:
      node = node[k]
    node[key[-1]] = val
    with open(f, "w") as fj:
      json.dump(content, fj, indent=2)