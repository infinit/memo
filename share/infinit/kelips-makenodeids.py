#! /usr/bin/env python3
import sys
import uuid
import hashlib
import json
import base64

if len(sys.argv) < 2 or sys.argv[1] == '-h' or sys.argv[1] == '--help':
  print("""usage: makenodeids files...
    Write an unique node id to each of the kelips configuration files passed
    in arguments""")
  sys.exit(0)
for f in sys.argv[1:]:
  v = str(uuid.uuid4())
  print(v)
  sha = hashlib.sha256(v.encode('latin-1')).digest()
  encoded = base64.b64encode(sha)
  print(encoded)
  with open(f, "r") as fj:
    content = json.load(fj)
  content['overlay']['config']['node_id'] = encoded.decode('utf-8')
  with open(f, "w") as fj:
    json.dump(content, fj, indent=2)