#include <infinit/filesystem/Unknown.hh>
#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/File.hh>
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
    Unknown::Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name)
      : Node(owner, parent, name)
    {}

    void
    Unknown::mkdir(mode_t mode)
    {
      ELLE_DEBUG("mkdir %s", _name);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      auto address = b->address();
      if (_parent->_inherit_auth)
      {
        ELLE_DEBUG("Inheriting auth");
        ELLE_ASSERT(!!_parent->_block);
        // We must store first to ready ACL layer
        _owner.store_or_die(*b, model::STORE_INSERT);
        umbrella([&] { _parent->_block->copy_permissions(*b);});
        Directory d(_parent, _owner, _name, address);
        d._block = std::move(b);
        d._inherit_auth = true;
        d._push_changes({OperationType::insert, ""});
      }
      else
        _owner.store_or_die(std::move(b), model::STORE_INSERT);
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      _parent->_files.insert(
        std::make_pair(_name,
                       std::make_pair(EntryType::directory, address)));
      _parent->_commit({OperationType::insert, _name});
      _remove_from_cache();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      ELLE_ASSERT_EQ(signed(mode & S_IFMT), S_IFREG);
      if (!_owner.single_mount())
        _parent->_fetch();
      if (_parent->_files.find(_name) != _parent->_files.end())
      {
        ELLE_WARN("File %s exists where it should not", _name);
        _remove_from_cache();
        auto f = std::dynamic_pointer_cast<File>(_owner.filesystem()->path(full_path().string()));
        return f->open(flags, mode);
      }
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      //optimize: dont push block yet _owner.block_store()->store(*b);
      ELLE_DEBUG("Adding file to parent %x", _parent.get());
      _parent->_files.insert(
        std::make_pair(_name, 
          std::make_pair(EntryType::file, b->address())));
      _parent->_commit({OperationType::insert, _name}, true);
      elle::SafeFinally remove_from_parent( [&] {
          _parent->_files.erase(_name);
          try
          {
            _parent->_commit({OperationType::remove, _name}, true);
          }
          catch(...)
          {
            ELLE_WARN("Rollback failure on %s", _name);
          }
      });
      _remove_from_cache();
      auto raw = _owner.filesystem()->path(full_path().string());
      auto f = std::dynamic_pointer_cast<File>(raw);
      if (!f)
        ELLE_ERR("Expected valid pointer from %s(%s), got nullptr",
                 raw.get(), typeid(*raw).name());
      f->_first_block = std::move(b);
      f->_first_block_new = true;
      f->_header = FileHeader(0, 1, S_IFREG | 0666,
                              time(nullptr), time(nullptr), time(nullptr),
                              File::default_block_size);
      if (_parent->_inherit_auth)
      {
        umbrella([&] { _parent->_block->copy_permissions(
          dynamic_cast<ACLBlock&>(*f->_first_block));
        });
      }
      f->_commit();
      _owner.filesystem()->set(f->full_path().string(), f);
      std::unique_ptr<rfs::Handle> handle(new FileHandle(f, true, true, true));
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
      _owner.store_or_die(std::move(b));
      auto it =_parent->_files.insert(
        std::make_pair(_name, std::make_pair(EntryType::symlink, addr)));
      _parent->_commit({OperationType::insert, _name}, true);
      _remove_from_cache();
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
      THROW_NOENT;
    }

    void
    Unknown::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Unknown(\"%s\")", this->_name);
    }


  }
}
