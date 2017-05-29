#! /usr/bin/env python3
import sys
import json

def pacify(j):
  if j['operation'] == 'stat' and j['success'] == True:
    j['st_ctime'] = 0
    j['st_mtime'] = 0
    j['st_atime'] = 0
    j['st_ino'] = 0
    
with open(sys.argv[1], "r") as f1:
  with open(sys.argv[2], "r") as f2:
    for line in zip(f1, f2):
      j1 = json.loads(line[0])
      j2 = json.loads(line[1])
      pacify(j1)
      pacify(j2)
      if j1 != j2:
        print("-%s\n+%s" % (j1, j2))
