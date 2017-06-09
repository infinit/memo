<br>

#### Prerequisites

- Have configured your S3 compatible service.
- Have the *region*, *bucket*, *endpoint*, *Access Key ID* and *Secret Access Key* for the service accessible.
- <a href="${route('doc_get_started')}">Infinit installed</a>.
- An <a href="${route('doc_reference')}#user">Infinit user</a>.

Services such as Minio give you some of the information you need when you start them up:

```
Endpoint:  http://192.168.0.17:9000  http://127.0.0.1:9000
AccessKey: J8BHCPZMUFW4P4IUL5E6
SecretKey: +eqc0hKEmI00CzdN0T7PtESjElpYGML7Aw07CZvN
Region:    us-east-1
SqsARNs:

Browser Access:
   http://192.168.0.17:9000  http://127.0.0.1:9000
```

_**NOTE**: With Minio, you will need to create a new bucket. This can be done either using the web interface or the Minio CLI._

Adding service credentials to Infinit
-------------------------------------

First, you will need to add your service credentials to Infinit. This can be done using the `infinit credentials` binary.

```
$> infinit credentials add --as alice --aws --name minio
Please enter your AWS credentials
Access Key ID: AKIAIOSFODNN7EXAMPLE
Secret Access Key: ****************************************
Locally stored AWS credentials "minio".
```
_**NOTE**: Credentials are only ever stored locally and cannot be pushed to the Hub._

Creating the Infinit silo
-------------------------

With the service's credentials added to Infinit, you can now create the silo.

```
$> infinit silo create minio-storage --s3 --account minio --bucket my-bucket --endpoint http://192.168.0.17:9000 --region us-east-1
Create storage "minio-storage".
```
