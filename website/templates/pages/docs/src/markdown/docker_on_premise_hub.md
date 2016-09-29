On Premise Hub
==============

The on premise Hub gives you all the benefits of using the Hub while ensuring that your data never leaves your infrastructure.

Operation of the Hub
--------------------

The on premise Hub is provided as a Docker container which includes CouchDB as the data store and NGINX for handling HTTP/HTTPS requests.

The Hub exposes a REST API for storing and fetching Infinit object descriptors. These descriptors are JSON dictionaries used to define Infinit users and infrastructure. As some descriptors contain sensitive information, communication with the Hub should be secured with HTTPS.

Prerequisites
-------------

### Software ###

A Docker container ensures that the Hub only relies on Docker. It was, however, tested using:

- Ubuntu 16.04 LTS
- [Docker 1.12](https://docs.docker.com/engine/installation/linux/ubuntulinux/)

### Other ###

It is recommended (but optional) to use HTTPS with the on premise Hub. To do this, you will need an SSL certificate and key. The certificate may currently be [self signed](http://www.akadia.com/services/ssh_test_certificate.html) but this may change in a future release.

Running the on premise Hub
--------------------------

### Attached volumes ###

In order to ensure that the data stored by the Hub is persistent after the Hub's container has been destroyed, a volume needs to be attached to `/couchdb`.

To add certificates for providing the Hub over HTTPS, a volume must be attached to `/certificates`. The folder must include a certificate and its key. The relative paths of these files are then specified using the `--ssl-certificate` and `--ssl-certificate-key` options respectively. Note that only the path relative to the attached volume is required, e.g.: if the certificate is in the folder root and named *beyond.crt* then the option passed would be `--ssl-certificate beyond.crt`.

Logs are also provided through a volume attached to `/log`. Inside this folder, NGINX, uWSGI and CouchDB logs can be found. The logs can also be disabled using the `--disable-logs` option.

### Launching ###

The default entrypoint of the container is a Python script to which several different options can be passed to configure the Hub. These options are described below:

- `--server-name`: Hostname that the server will be accessible with. When using HTTPS, this must match the name in the certificate.
- `--ssl-certificate`: Relative path of certificate in the `certificates` volume.
- `--ssl-certificate-key`: Relative path of certificate key in `certificates` volume.
- `--ldap-server`: Server to use for LDAP operations.
- `--keep-deleted-users`: Keep user information when deleted.
- `--disable-logs`: Do not write logs.

An example of how to run the container is given below. Note that it is run in *interactive* mode with a *tty* and the `--rm` switch which will aid in debugging options that are passed. It can also be launched in the background by replacing `--interactive --tty --rm` with `--detach`.

```
$> docker run --interactive --tty --rm                 \
    --name infinit-hub                                 \
    --volume /host/path/to/couchdb:/couchdb            \
    --volume /host/path/to/certificates:/certificates  \
    --volume /host/path/to/log:/log                    \
    --publish 80:80                                    \
    --publish 443:443                                  \
    infinit-beyond:0.6.2-42-g63d92b1                   \
      --server-name your.server.name                   \
      --ssl-certificate server.crt                     \
      --ssl-certificate-key server.key                 \
      --ldap-server ldap.company.com
```

### Testing ###

Once you have configured the Hub, you should be able to access it using `https://your.server.name`. If it is working correctly, it will reply with the version.

```
$> curl -k https://your.server.name
{"version": "0.6.2"}
```

Using an on premise Hub
-----------------------

The Hub used by the Infinit binaries can be set using the `INFINIT_BEYOND` environment variable.

```
$> INFINIT_BEYOND=https://your.server.name infinit-user --signup --email your@email.com
```

It may be more convenient to *export* the variable in your terminal's initialization script so that it is always set.

```
export INFINIT_BEYOND=https://your.server.name
```
