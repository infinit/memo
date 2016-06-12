FROM ubuntu
MAINTAINER Infinit <contact@infinit.sh>

RUN apt-get update
RUN apt-get install -y python3
RUN apt-get install -y python3-bottle

ADD main.html /root
ADD graphed.py /root
ADD viz.js /root

WORKDIR /root
CMD python3 graphed.py

