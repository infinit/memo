#include <infinit/filesystem/Unknown.hh>

#include <elle/cast.hh>

#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Unreachable.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <reactor/filesystem.hh>

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif
#ifdef INFINIT_WINDOWS
  #undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.Unknown");

namespace infinit
{
  namespace filesystem
  {
    class DummyConflictResolver: public model::ConflictResolver
    {
    public:
      DummyConflictResolver() {}
      DummyConflictResolver(elle::serialization::SerializerIn& s) {}
      void serialize(elle::serialization::Serializer& s,
                     elle::Version const&) override
      {
      }
      std::unique_ptr<Block>
      operator() (Block& block,
                  Block& current,
                  model::StoreMode mode) override
      {
        return current.clone();
      }
    };
    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<DummyConflictResolver> _register_dcr("dummy");

    static
    std::unique_ptr<model::ConflictResolver> dummy_conflict_resolver()
    {
      return elle::make_unique<DummyConflictResolver>();
    }

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
        throw rfs::Error(EACCES, "Access denied.");
      auto b = this->_owner.block_store()->
        make_block<infinit::model::blocks::ACLBlock>();
      auto address = b->address();
      std::unique_ptr<ACLBlock> parent_block;
      if (this->_parent->inherit_auth())
      {
        ELLE_DEBUG_SCOPE("inheriting auth");
        parent_block = elle::cast<ACLBlock>::runtime(
          this->_owner.block_store()->fetch(_parent->address()));
        umbrella([&] { parent_block
          ->copy_permissions(dynamic_cast<ACLBlock&>(*b));});
        DirectoryData dd {this->_parent->_path / _name, address};
        dd._inherit_auth = true;
        dd.write(*_owner.block_store(), Operation{OperationType::update, "/inherit"}, b, true, true);
      }
      else
        this->_owner.store_or_die(std::move(b), model::STORE_INSERT,
                                  dummy_conflict_resolver());
      ELLE_ASSERT_EQ(this->_parent->_files.find(this->_name),
                     this->_parent->_files.end());
      this->_parent->_files.emplace(
        this->_name, std::make_pair(EntryType::directory, address));
      elle::SafeFinally revert([&] {
          this->_parent->_files.erase(this->_name);
          this->_owner.unchecked_remove(address);
      });
      this->_parent->write(
        *_owner.block_store(),
        {OperationType::insert, this->_name, EntryType::directory, address},
        parent_block, true);
      revert.abort();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      if (_owner.read_only())
        throw rfs::Error(EACCES, "Access denied.");
      mode |= S_IFREG;
      if (_parent->_files.find(_name) != _parent->_files.end())
      {
        ELLE_WARN("File %s exists where it should not", _name);
        File f(_owner, _parent->_files.at(_name).second, {}, _parent, _name);
        return f.open(flags, mode);
      }
      auto parent_block = this->_owner.block_store()->fetch(_parent->address());
      _owner.ensure_permissions(*parent_block, true, true);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      //optimize: dont push block yet _owner.block_store()->store(*b);
      ELLE_DEBUG("Adding file to parent %x", _parent.get());
      _parent->_files.insert(
        std::make_pair(_name,
          std::make_pair(EntryType::file, b->address())));
      _parent->write(*_owner.block_store(),
                     Operation{OperationType::insert, _name, EntryType::file, b->address()},
                     DirectoryData::null_block,
                     true);
      elle::SafeFinally remove_from_parent( [&] {
          _parent->_files.erase(_name);
          try
          {
            _parent->write(*_owner.block_store(),
                           Operation{OperationType::remove, _name});
          }
          catch(...)
          {
            ELLE_WARN("Rollback failure on %s", _name);
          }
      });
      FileData fd(_parent->_path / _name, b->address(), mode & 0700);
      if (_parent->inherit_auth())
      {
        umbrella([&] { dynamic_cast<ACLBlock*>(parent_block.get())->copy_permissions(
          dynamic_cast<ACLBlock&>(*b));
        });
      }
      fd.write(*_owner.block_store(), WriteTarget::all, b, true);
      std::unique_ptr<rfs::Handle> handle(
        new FileHandle(*_owner.block_store(), fd, true, true, true));
      remove_from_parent.abort();
      return handle;
    }

    void
    Unknown::symlink(boost::filesystem::path const& where)
    {
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      FileHeader fh(0, 1, S_IFLNK | 0666,
                    time(nullptr), time(nullptr), time(nullptr),
                    0);
      fh.symlink_target = where.string();
      auto serdata = elle::serialization::binary::serialize(fh);
      b->data(serdata);
      auto addr = b->address();
      _owner.store_or_die(std::move(b), model::STORE_INSERT);
      this->_parent->_files.emplace(
        this->_name, std::make_pair(EntryType::symlink, addr));
      _parent->write(*_owner.block_store(),
                     Operation{OperationType::insert, _name, EntryType::symlink, addr},
                     DirectoryData::null_block, true);
    }

    void
    Unknown::link(boost::filesystem::path const& where)
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

    Unreachable::Unreachable(FileSystem& owner, std::shared_ptr<DirectoryData> parent,
                             std::string const& name,
                             Address address,
                             EntryType type)
    : _type(type)
    {}
    void
    Unreachable::stat(struct stat* st)
    {
      memset(st, 0, sizeof(struct stat));
      st->st_mode = _type == EntryType::file ? S_IFREG : S_IFDIR;
    }
  }
}
