#! /bin/bash

# load if needed a bunch of image files, of the form repository-shorthash.tgz

for f in *.tgz; do
  repo=$(echo $f |sed -e 's/-.*$//')
  hash=$(echo $f |sed -e 's/^.*-//' -e 's/.tgz$//')
  echo "checking $repo version $hash"
  effective=$(docker images |grep "^$repo" |grep latest | awk '{print $3}')
  if test "$effective" != "$hash"; then
    echo "updating $repo from $effective"
    id=$(docker load -q < $f | cut -d: -f 3)
    docker tag $id $repo:latest
  fi
done