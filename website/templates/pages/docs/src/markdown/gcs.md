Creating a storage using Google Cloud Storage
=============================================

This will guide you through setting up a Google Cloud Storage bucket as a storage resource for Infinit.

### Prerequisites ###

- A [Google Cloud Platform](https://cloud.google.com) account with administrator privileges.
- <a href="${route('doc_get_started')}">Infinit installed</a>.
- An <a href="${route('doc_get_started')}"#create-a-user>Infinit user</a>.

### Creating a GCS bucket ###

Navigate from the Google Cloud Platform console to *Storage*. Here you can click *Create Bucket* to create a new bucket. As GCS buckets must be named uniquely across the entire platform, it's good practice to use your domain name and optionally the region the bucket is hosted in the name.

<img src="${url('images/docs/gcs/create-bucket.png')}" alt="Google Cloud Platform console create bucket popup">

Make a note of the bucket name that you chose as you will need this later.

_**IMPORTANT**: Choose your bucket region to be closest to where you will be using it from. This will ensure that you have higher transfer speeds and lower latency when accessing your Infinit volume._

### Add GCS credentials to Infinit ###

We can now add the GCS credentials to Infinit. This process uses [OAuth](https://en.wikipedia.org/wiki/OAuth) and requires you to navigate to a link provided from the command line.

```
$> infinit-credentials --add --as alice --gcs
Register your Google account with infinit by visiting https://beyond.infinit.io/users/alice/gcs-oauth
```

Navigate to this link with your Web browser and allow Infinit to access your account.

<img src="${url('images/docs/gcs/oauth-permissions.png')}" alt="Google Cloud Platform OAuth permissions">

Infinit is now allowed access but you need to fetch the credentials from the Hub.

```
$> infinit-credentials --fetch --as alice
Fetched Google Cloud Storage credentials alice@example.com (Alice)
```

Make note of the email address associated with the account as this is used to reference the credentials in the next step.

### Creating the Infinit storage resource ###

Now that the bucket has been created and Infinit has the GCS credentials you can create the storage resource.

```
$> infinit-storage --create --gcs --name gcs-storage --account "alice@example.com" --bucket infinit-gcs-storage-eu-example-com --path blocks-folder
Created storage "gcs-storage".
```
