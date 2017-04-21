#include <infinit/filesystem/Unknown.hh>

#include <elle/cast.hh>

#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Unreachable.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <elle/reactor/filesystem.hh>

#include <sys/stat.h> // S_IMFT...
#include <fcntl.h> // O_EXCL

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif
#ifdef INFINIT_WINDOWS
  #undef stat
  #define O_EXCL _O_EXCL
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.Unknown");

namespace infinit
{
  namespace filesystem
  {
    struct NewFolderResolver
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;

      NewFolderResolver(bfs::path const& path)
        : _path(path.string())
      {}

      NewFolderResolver(elle::serialization::Serializer& s,
                        elle::Version const& version)
        : Super() // Do not call Super(s, version)
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
        s.serialize("path", this->_path);
      }

      std::string
      description() const override
      {
        return elle::sprintf("create directory %s", this->_path);
      }

      ELLE_ATTRIBUTE(std::string, path);
    };

    static const elle::serialization::Hierarchy<infinit::model::ConflictResolver>::
    Register<NewFolderResolver> _register_new_folder_resolver(
      "NewFolderResolver");


    Unknown::Unknown(FileSystem& owner,
                     std::shared_ptr<DirectoryData> parent,
                     std::string const& name)
      : Node(owner, model::Address::null, parent, name)
    {}

    void
    Unknown::mkdir(mode_t mode)
    {
      ELLE_TRACE_SCOPE("%s: make directory", *this);
      if (_owner.read_only())
        THROW_ACCES();
      auto b = this->_owner.block_store()->
        make_block<infinit::model::blocks::ACLBlock>();
      auto address = b->address();
      std::unique_ptr<ACLBlock> parent_block;
      if (this->_parent->inherit_auth())
      {
        umbrella([&] {
            ELLE_DEBUG_SCOPE("inheriting auth");
            parent_block = elle::cast<ACLBlock>::runtime(
              this->_owner.block_store()->fetch(_parent->address()));
            parent_block->copy_permissions(dynamic_cast<ACLBlock&>(*b));
        });
        DirectoryData dd {this->_parent->_path / _name, address};
        dd._inherit_auth = true;
        dd.write(_owner, Operation{OperationType::update, "/inherit"}, b, true, true);
      }
      else
        this->_owner.store_or_die(
          std::move(b), true,
          std::make_unique<NewFolderResolver>(
            (this->_parent->_path / _name)));
      ELLE_ASSERT_EQ(this->_parent->_files.find(this->_name),
                     this->_parent->_files.end());
      this->_parent->_files.emplace(
        this->_name, std::make_pair(EntryType::directory, address));
      elle::SafeFinally revert([&] {
          this->_parent->_files.erase(this->_name);
          this->_owner.unchecked_remove(address);
      });
      this->_parent->write(
        _owner,
        {OperationType::insert, this->_name, EntryType::directory, address},
        parent_block, true);
      revert.abort();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create_0_7(int flags, mode_t mode)
    {
      std::unique_ptr<rfs::Handle> handle;
      auto parent_block = this->_owner.block_store()->fetch(_parent->address());
      _owner.ensure_permissions(*parent_block, true, true);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      // Add pending entry to parent so we can't leak b
      _parent->_files.insert(
        std::make_pair(_name,
          std::make_pair(EntryType::pending, b->address())));
      _parent->write(_owner,
                     Operation{
                       (flags & O_EXCL) ? OperationType::insert_exclusive : OperationType::insert,
                       _name, EntryType::pending, b->address()},
                     DirectoryData::null_block,
                     true);
      // arm rollback
      elle::With<elle::Finally>(
        [&]
        {
          this->_parent->_files.erase(_name);
          try
          {
            _parent->write(_owner,
                           Operation{OperationType::remove, _name});
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("rollback failure on %s", this->_name);
          }
        }) << [&] (elle::Finally& remove_from_parent)
      {

        // Handle acl inheritance
        FileData fd(_parent->_path / _name, b->address(), mode & 0700,
          _owner.block_size().value_or(File::default_block_size));
        if (_parent->inherit_auth())
        {
          umbrella(
            [&]
            {
              dynamic_cast<ACLBlock*>(parent_block.get())->copy_permissions(
                dynamic_cast<ACLBlock&>(*b));
            });
        }
        // push b
        fd.write(_owner, WriteTarget::all, b, true);
        // arm rollback
        elle::With<elle::Finally>(
        [&]
        {
          try
          {
            _owner.block_store()->remove(b->address());
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("rollback failure on %s", this->_name);
          }
        }) << [&] (elle::Finally& remove_block)
        {
          // push definitive entry to directory
          _parent->_files[_name] = std::make_pair(EntryType::file, b->address());
          _parent->write(_owner,
                         Operation{
                           OperationType::insert,
                           _name, EntryType::file, b->address()},
                         DirectoryData::null_block,
                         true);
          remove_block.abort();
          remove_from_parent.abort();
          handle.reset(
            new FileHandle(_owner, fd, true));
        };
      };
      return handle;
    }


    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      if (_owner.read_only())
        THROW_ACCES();
      mode |= S_IFREG;
      if (_parent->_files.find(_name) != _parent->_files.end())
      {
        if (flags & O_EXCL)
          THROW_EXIST();
        ELLE_WARN("File %s exists where it should not", _name);
        File f(_owner, _parent->_files.at(_name).second, {}, _parent, _name);
        return f.open(flags, mode);
      }
      if (_owner.block_store()->version() >= elle::Version(0, 7, 0))
        return this->create_0_7(flags, mode);
      auto parent_block = this->_owner.block_store()->fetch(_parent->address());
      _owner.ensure_permissions(*parent_block, true, true);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      //optimize: dont push block yet _owner.block_store()->store(*b);
      ELLE_DEBUG("Adding file to parent %x", _parent.get());
      _parent->_files.insert(
        std::make_pair(_name,
          std::make_pair(EntryType::file, b->address())));
      _parent->write(_owner,
                     Operation{
                       (flags & O_EXCL) ? OperationType::insert_exclusive : OperationType::insert,
                       _name, EntryType::file, b->address()},
                     DirectoryData::null_block,
                     true);
      std::unique_ptr<rfs::Handle> handle;
      elle::With<elle::Finally>(
        [&]
        {
          this->_parent->_files.erase(_name);
          try
          {
            _parent->write(_owner,
                           Operation{OperationType::remove, _name});
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("rollback failure on %s", this->_name);
          }
        }) << [&] (elle::Finally& remove_from_parent)
      {
        FileData fd(_parent->_path / _name, b->address(), mode & 0700,
                    _owner.block_size().value_or(File::default_block_size));
        if (_parent->inherit_auth())
        {
          umbrella(
            [&]
            {
              dynamic_cast<ACLBlock*>(parent_block.get())->copy_permissions(
                dynamic_cast<ACLBlock&>(*b));
            });
        }
        fd.write(_owner, WriteTarget::all, b, true);
        handle.reset(
          new FileHandle(_owner, fd, true));
        remove_from_parent.abort();
      };
      return handle;
    }

    struct NewSymlinkResolver
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;
      NewSymlinkResolver(bfs::path const& source,
                         bfs::path const& destination)
        : Super()
        , _source(source.string())
        , _destination(destination.string())
      {}

      NewSymlinkResolver(elle::serialization::Serializer& s,
                    elle::Version const& version)
        : Super() // Do not call Super(s, version)
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
        s.serialize("source", this->_source);
        s.serialize("destination", this->_destination);
      }

      std::string
      description() const override
      {
        return elle::sprintf("create symlink from %s to %s",
                             this->_source, this->_destination);
      }

      ELLE_ATTRIBUTE(std::string, source);
      ELLE_ATTRIBUTE(std::string, destination);
    };

    static const elle::serialization::Hierarchy<infinit::model::ConflictResolver>::
    Register<NewSymlinkResolver> _register_new_symlink_resolver(
      "NewSymlinkResolver");

    void
    Unknown::symlink(bfs::path const& where)
    {
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      auto parent_block = this->_owner.block_store()->fetch(_parent->address());
      _owner.ensure_permissions(*parent_block, true, true);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      auto now = time(nullptr);
      FileHeader fh(0, 1, S_IFLNK | 0600,
                    now, now, now, now,
                    0);
      fh.symlink_target = where.string();
      auto serdata = elle::serialization::binary::serialize(fh);
      b->data(serdata);
      if (_parent->inherit_auth())
      {
        umbrella([&] { dynamic_cast<ACLBlock*>(parent_block.get())->copy_permissions(
          dynamic_cast<ACLBlock&>(*b));
        });
      }
      auto addr = b->address();
      _owner.store_or_die(
        std::move(b), true,
        std::make_unique<NewSymlinkResolver>(this->_parent->_path / this->_name,
                                              where));
      this->_parent->_files.emplace(
        this->_name, std::make_pair(EntryType::symlink, addr));
      _parent->write(_owner,
                     Operation{OperationType::insert, _name, EntryType::symlink, addr},
                     DirectoryData::null_block, true);
    }

    void
    Unknown::link(bfs::path const& where)
    {
      throw rfs::Error(ENOENT, "link source does not exist");
    }

    void
    Unknown::stat(struct stat*)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      throw rfs::Error(ENOENT, "No such file or directory", elle::Backtrace());
    }

    void
    Unknown::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Unknown(\"%s\")", this->_name);
    }
  }
}
