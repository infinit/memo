Creating a storage using the local filesystem
=============================================

This will guide you through setting up a storage resource that uses a locally accessible filesystem directory.

Prerequisites
-------------

- <a href="${route('doc_get_started')}">Infinit installed</a>.
- An <a href="${route('doc_reference')}#user">Infinit user</a>.

Creating the Infinit storage resource
-------------------------------------

An Infinit storage resource can be created using any locally accessible filesystem folder indicated with the `--path` flag. Note that should the `--path` be omitted, the blocks of encrypted data would be stored at a defaut location within the $INFINIT_HOME/blocks/<storage name>. In addition, the capacity can be limited through the `--capacity` flag while omitting it will make the storage unlimited.

```
$> infinit-storage --create --filesystem --name filesystem-storage --path /path/to/folder --capacity 10GB
Created storage "filesystem-storage".
```
