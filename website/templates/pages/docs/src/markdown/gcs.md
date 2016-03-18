Creating a storage using Google Cloud Storage
=============================================

This will guide you through setting up a Google Cloud Storage (GCS) bucket as a storage resource for Infinit.

Prerequisites
-------------

- A [Google Cloud Platform](https://cloud.google.com) account with administrator privileges.
- <a href="${route('doc_get_started')}">Infinit installed</a>.
- An <a href="${route('doc_reference')}#sign-up-on-the-hub">Infinit user</a>.

Creating a GCS bucket
---------------------

Navigate from the <a href="https://console.cloud.google.com">Google Cloud Platform console</a> to _Storage_. Here you can click _Create Bucket_ to create a new bucket. As GCS buckets must be named uniquely across the entire platform, it's good practice to use your domain name and optionally the region the bucket is hosted in the name. Make a note of the bucket name that you choose as you will need this later.

<img src="${url('images/docs/gcs/create-bucket.png')}" alt="Google Cloud Platform console create bucket popup">

_**IMPORTANT**: Choose your bucket region to be closest to where you will be using it from. This will ensure that you have higher transfer speeds and lower latency when accessing your Infinit volume._

Add GCS credentials to Infinit
------------------------------

We can now add the GCS credentials to Infinit. This process uses [OAuth](https://en.wikipedia.org/wiki/OAuth) and requires you to navigate to a link provided from the command line.

```
$> infinit-credentials --add --as alice --gcs
Register your Google account with infinit by visiting https://beyond.infinit.io/users/alice/gcs-oauth
```

Navigate to this link with your Web browser and allow Infinit to access your account.

<img src="${url('images/docs/gcs/oauth-permissions.png')}" alt="Google Cloud Platform OAuth permissions">

Infinit is now allowed access to your bucket. You however need to fetch the credentials from the Hub in order to proceed.

```
$> infinit-credentials --fetch --as alice
Fetched Google Cloud Storage credentials alice@example.com (Alice)
```

Make note of the email address associated with the account as this is used to reference the credentials in the next step.

Creating the Infinit storage resource
-------------------------------------

Now that the bucket has been created and Infinit has the GCS credentials, you can create the storage resource.

Take care of referencing the bucket name you chose in the first step through the `--bucket` option along with the name of the credentials you just registered i.e the email address associated with your GCS account, in this example _alice@example.com_. Finally, the `--path` option can be used to specify a folder within the bucket to hold the blocks of encrypted data that Infinit will store.

```
$> infinit-storage --create --gcs --name gcs-storage --account "alice@example.com" --bucket infinit-gcs-storage-eu-example-com --path blocks-folder
Created storage "gcs-storage".
```
