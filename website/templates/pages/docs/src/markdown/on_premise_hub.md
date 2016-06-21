On Premise Hub
==============

The on premise Hub gives you all the benefits of using [the Hub](xxx link) while ensuring that your data never leaves your infrastructure.

Operation of the Hub
--------------------

The Hub exposes a REST API for storing and fetching Infinit object descriptors. These descriptors are JSON dictionaries used to define Infinit users and infrastructure. As some descriptors contain sensitive information, communication with the Hub should be secured with HTTPS.

Prerequisites
-------------

### Software ###

The Hub currently supports Ubuntu 14.04 LTS or higher. The following packages are additionally required:

- couchdb
- infinit
- libjpeg-dev
- nginx
- python3
- python3-crypto
- python3-httplib2
- python3-pil
- python3-pip
- python3-requests
- uwsgi
- uwsgi-plugin-python3

Additional Python dependencies installed with pip3:
- oauth2client==1.4.7

### Other ###

- SSL certificate and key (may be [self signed](http://www.akadia.com/services/ssh_test_certificate.html) but this may change).
- Domain name with correct routing.

Configuration
-------------

### CouchDB ###

If you are using Ubuntu 16.04, you will need to ensure that CouchDB starts on boot:

```
$> sudo update-rc.d couchdb defaults
$> sudo update-rc.d couchdb enable
```

### uWSGI setup ###

Enable the Hub in *uWSGI* by adding it to the `apps-enabled` directory using a symbolic link:

```
$> sudo ln -s /etc/uwsgi/apps-available/beyond.xml /etc/uwsgi/apps-enabled/beyond.xml
```

### nginx setup ###

Edit the `/etc/nginx/sites-available/beyond` file as follows:

- Set `server_name` to your desired domain name.
- Set `ssl_certificate` to the path where your SSL certificate is.
- Set `ssl_certificate_key` to the path where your SSL certificate key is.

Ensure that the log directory exists:

```
$> sudo mkdir /var/log/nginx/beyond
```

Enable the Hub in *nginx* by adding it to the `sites-enabled` directory using a symbolic:

```
$> sudo ln -s /etc/nginx/sites-available/beyond /etc/nginx/sites-enabled/beyond
```

Remove the default website if it is there:

```
$> sudo rm /etc/nginx/sites-enabled/default
```

### Ensure that the services are running ###

Now that everything is configured, you can restart all the required services:

```
$> sudo service couchdb start
$> sudo service uwsgi restart
$> sudo service nginx restart
```

Test
----

Once you have configured the Hub, you should be able to access it using `https://your.server.name`. If it is working correctly, it will reply with the version.

```
$> curl -k https://your.server.name
{"version": "0.6.0"}
```

Using an on premise Hub
-----------------------

The Hub used by the Infinit binaries can be set using the `INFINIT_BEYOND` environment variable.

```
$> INFINIT_BEYOND=https://your.server.name infinit-user --signup --email your@email.com
```

It may be more convenient to *export* the variable in your terminal's initialisation script so that it is always set.

```
export INFINIT_BEYOND=https://your.server.name
```
