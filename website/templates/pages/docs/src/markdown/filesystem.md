Creating a storage using the local filesystem
=============================================

This will guide you through setting up a storage resource that uses a locally accessible filesystem folder.

### Prerequisites ###

- <a href="${route('doc_get_started')}">Infinit installed</a>.
- An <a href="${route('doc_get_started')}"#create-a-user>Infinit user</a>.

### Creating the Infinit storage resource ###

An Infinit storage resource can be created using any locally accessible filesystem folder indicated with the `--path` flag. Note that the capacity can be limited using the `--capacity` flag and that omitting this flag will make the storage unlimited.

```
infinit-storage --create --filesystem --name filesystem-storage --path /path/to/folder --capacity 10GB
Created storage "filesystem-storage".
```
