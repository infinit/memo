#! /bin/bash

hosts="docker1 docker2 docker3 docker4"

mkdir -p /tmp/isaal
rm -rf /tmp/isaal/*
for h in $hosts; do
  scp root@$h:/tmp/run.log.* /tmp/isaal/
done
for f in /tmp/isaal/*; do
  id=$(echo $f | cut -d. -f 7 |egrep -o '^........')
  sed -i -e "s/:\\s/ $id /" $f
done

sort -mn /tmp/isaal/* > /tmp/isaal.log