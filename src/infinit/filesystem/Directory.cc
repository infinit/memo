#include <infinit/filesystem/Directory.hh>
#include <elle/cast.hh>
#include <elle/os/environ.hh>
#include <reactor/exception.hh>

#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>
#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/xattribute.hh>

#include <elle/serialization/binary.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
// #include <infinit/filesystem/FileHandle.hh>

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_WINDOWS
#undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.Directory");

namespace elle
{
  namespace serialization
  {
    template<> struct Serialize<infinit::filesystem::EntryType>
    {
      typedef int Type;
      static int convert(infinit::filesystem::EntryType& et)
      {
        return (int)et;
      }
      static infinit::filesystem::EntryType convert(int repr)
      {
        return (infinit::filesystem::EntryType)repr;
      }
    };
  }
}
namespace infinit
{
  namespace filesystem
  {
    std::unique_ptr<Block>
    resolve_directory_conflict(Block& b,
                               Block& current,
                               model::StoreMode store_mode,
                               boost::filesystem::path p,
                               FileSystem& owner,
                               Operation op,
                               std::weak_ptr<Directory> wd)
    {
       ELLE_TRACE("edit conflict on %s (%s %s)",
                  b.address(), op.type, op.target);
       Directory d({}, owner, "", b.address());
       d._fetch(elle::cast<ACLBlock>::runtime(current.clone()));
       switch(op.type)
       {
       case OperationType::insert:
         ELLE_ASSERT(!op.target.empty());
         if (d._files.find(op.target) != d._files.end())
         {
           ELLE_LOG("Conflict: the object %s was also created remotely,"
             " your changes will overwrite the previous content.",
             p / op.target);
         }
         ELLE_TRACE("insert: Overriding entry %s", op.target);
         d._files[op.target] = std::make_pair(op.entry_type, op.address);
         break;
       case OperationType::update:
         if (op.target == "")
         {
           ELLE_LOG("Conflict: the directory %s was updated remotely, your"
                    " changes will be dropped.", p);
           break;
         }
         else if (op.target == "/inherit")
         {
           d._inherit_auth = true;
           break;
         }
         else if (op.target == "/disinherit")
         {
           d._inherit_auth = false;
           break;
         }
         else if (d._files.find(op.target) == d._files.end())
         {
           ELLE_LOG("Conflict: the object %s (%s / %s) was removed remotely,"
             " your changes will be dropped.",
             p / op.target, p, op.target);
           if (!wd.expired())
           {
             auto sd = wd.lock();
             sd->_files.erase(op.target);
           }
           break;
         }
         else if (d._files[op.target].second != op.address)
         {
           ELLE_LOG("Conflict: the object %s was replaced remotely,"
             " your changes will be dropped.",
             p / op.target);
           if (!wd.expired())
           {
             auto sd = wd.lock();
             if (sd->_files.find(op.target) != sd->_files.end())
               sd->_files[op.target] = d._files[op.target];
           }
           break;
         }
         ELLE_TRACE("update: Overriding entry %s", op.target);
         d._files[op.target] = std::make_pair(op.entry_type, op.address);
         break;
       case OperationType::remove:
         d._files.erase(op.target);
         break;
       }
       elle::Buffer data;
       {
         elle::IOStream os(data.ostreambuf());
         elle::serialization::binary::SerializerOut output(os);
         output.serialize_forward(d);
       }
       d._block->data(data);
       return std::move(d._block);
    }

    class DirectoryConflictResolver: public model::ConflictResolver
    {
    public:
      DirectoryConflictResolver(elle::serialization::SerializerIn& s)
      {
        serialize(s);
      }
      DirectoryConflictResolver(DirectoryConflictResolver&& b)
      : _path(b._path)
      , _owner(b._owner)
      , _owner_allocated(b._owner_allocated)
      , _op(b._op)
      , _wptr(b._wptr)
      {
        b._owner_allocated = false;
        b._owner = nullptr;
      }

      DirectoryConflictResolver()
      : _owner(nullptr)
      {}
      DirectoryConflictResolver(boost::filesystem::path p,
                                FileSystem* owner,
                                Operation op,
                                std::weak_ptr<Directory> wd)
        : _path(p)
        , _owner(owner)
        , _owner_allocated(false)
        , _op(op)
        , _wptr(wd)
      {}

      ~DirectoryConflictResolver()
      {
        if (_owner_allocated)
        {
          std::shared_ptr<infinit::model::Model> b = _owner->block_store();
          new std::shared_ptr<infinit::model::Model>(b);
          delete _owner;
        }
      }

      std::unique_ptr<Block>
      operator() (Block& block,
                  Block& current,
                  model::StoreMode mode) override
      {
        return resolve_directory_conflict(
          block, current, mode,
          this->_path, *this->_owner, this->_op, this->_wptr);
      }

      void serialize(elle::serialization::Serializer& s) override
      {
        std::string spath = _path.string();
        s.serialize("path", spath);
        _path = spath;
        s.serialize("optype", _op.type, elle::serialization::as<int>());
        s.serialize("optarget", _op.target);
        s.serialize("opaddr", _op.address);
        s.serialize("opetype", _op.entry_type, elle::serialization::as<int>());
        if (s.in())
        {
          infinit::model::Model* model = nullptr;
          const_cast<elle::serialization::Context&>(s.context()).get(model);
          ELLE_ASSERT(model);
          _owner_allocated = true;
          _owner = new FileSystem("", std::shared_ptr<model::Model>(model));
        }
      }

      boost::filesystem::path _path;
      FileSystem* _owner;
      bool _owner_allocated;
      Operation _op;
      std::weak_ptr<Directory> _wptr;
      typedef infinit::serialization_tag serialization_tag;
    };
    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<DirectoryConflictResolver> _register_dcr("dcr");

    void Directory::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("header", this->_header);
      s.serialize("content", this->_files);
      s.serialize("inherit_auth", this->_inherit_auth);
    }

    Directory::Directory(DirectoryPtr parent, FileSystem& owner,
                         std::string const& name,
                         Address address)
      : Node(owner, parent, name)
      , _address(address)
      , _inherit_auth(_parent?_parent->_inherit_auth : false)
      , _prefetching(false)
    {}

    void
    Directory::_fetch()
    {
      this->_fetch(nullptr);
    }

    void
    Directory::_fetch(std::unique_ptr<ACLBlock> block)
    {
      ELLE_TRACE_SCOPE("%s: fetch block", *this);
      if (block)
        this->_block = std::move(block);
      else if (this->_block)
      {
        auto block =
          elle::cast<ACLBlock>::runtime(
            this->_owner.fetch_or_die(this->_address, this->_block->version(), this));
        if (block)
          this->_block = std::move(block);
      }
      else
        this->_block = elle::cast<ACLBlock>::runtime(
          this->_owner.fetch_or_die(this->_address, {}, this));
      ELLE_TRACE("Got block");
      ELLE_DUMP("block: %s", *this->_block);
      std::unordered_map<std::string, std::pair<EntryType,Address>> local;
      std::swap(local, _files);
      bool empty = false;
      elle::IOStream is(
        umbrella([&] {
            auto& d = _block->data();
            ELLE_DUMP("block data: %s", d);
            empty = d.empty();
            return d.istreambuf();
          }, EACCES));
      if (empty)
      {
        ELLE_DEBUG("block is empty");
        _header = FileHeader(0, 1, S_IFDIR | 0666,
                             time(nullptr), time(nullptr), time(nullptr),
                             File::default_block_size);
        return;
      }
      elle::serialization::binary::SerializerIn input(is);
      try
      {
        input.serialize_forward(*this);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("Directory deserialization error: %s", e);
        std::swap(local, _files);
        throw rfs::Error(EIO, e.what());
      }
      ELLE_TRACE("Directory block fetch OK");
    }

    void
      Directory::statfs(struct statvfs * st)
      {
        memset(st, 0, sizeof(struct statvfs));
        st->f_bsize = 32768;
        st->f_frsize = 32768;
        st->f_blocks = 1000000;
        st->f_bavail = 1000000;
        st->f_bfree = 1000000;
        st->f_fsid = 1;
      }

    void
    Directory::_commit()
    {
      _commit(Operation{OperationType::update, "", EntryType::directory,
              Address::null}, false);
    }

    void
      Directory::_commit(Operation op, bool set_mtime)
      {
        ELLE_TRACE_SCOPE("%s: commit %s entries", *this, _files.size());
        if (set_mtime)
        {
          ELLE_DEBUG_SCOPE("set mtime");
          _header.mtime = time(nullptr);
          _header.ctime = time(nullptr);
        }
        elle::SafeFinally clean_cache([&] { _block.reset();});
        elle::Buffer data;
        {
          elle::IOStream os(data.ostreambuf());
          elle::serialization::binary::SerializerOut output(os);
          output.serialize_forward(*this);
        }
        ELLE_DUMP("content: %s", data);
        if (!_block)
          ELLE_DEBUG("fetch root block")
            _block = elle::cast<ACLBlock>::runtime(_owner.fetch_or_die(_address));
        _block->data(data);
        this->_push_changes(op);
        clean_cache.abort();
      }

    void
      Directory::_push_changes(Operation op, bool first_write)
      {
        ELLE_DEBUG_SCOPE("%s: store changes", *this);
        elle::SafeFinally clean_cache([&] { _block.reset();});
        auto address = _block->address();
        try
        {
          std::weak_ptr<Directory> wptr;
          // The directory can be a temp on the stack, in which case
          // shared_from_this will fail
          try
          {
            std::shared_ptr<Directory> dptr = std::dynamic_pointer_cast<Directory>(shared_from_this());
            wptr = dptr;
          }
          catch (std::bad_weak_ptr const&)
          {
          }
          ELLE_DEBUG("%s: store changes engage!", *this);
          _owner.block_store()->store(std::move(_block),
             first_write ? model::STORE_INSERT : model::STORE_ANY,
             elle::make_unique<DirectoryConflictResolver>(
               full_path(), &_owner, op, wptr));
          ELLE_ASSERT(!_block);
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          ELLE_TRACE("permission exception: %s", e.what());
          throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
        }
        catch(elle::Error const& e)
        {
          ELLE_WARN("unexpected elle error storing %x: %s",
                    address, e);
          throw rfs::Error(EIO, e.what());
        }
        clean_cache.abort();
      }

      std::shared_ptr<rfs::Path>
      Directory::child(std::string const& name)
      {
        ELLE_TRACE_SCOPE("%s: get child \"%s\"", *this, name);
        if (name == ".")
          return shared_from_this();
        // Alternate access to extended attributes
        static const char* attr_key = "$xattr.";
        if (name.size() > strlen(attr_key)
          && name.substr(0, strlen(attr_key)) == attr_key)
        {
          return std::make_shared<XAttributeFile>(shared_from_this(),
            name.substr(strlen(attr_key)));
        }
        if (name.size() > strlen("$xattrs.")
          && name.substr(0, strlen("$xattrs.")) == "$xattrs.")
        {
          auto c = child(name.substr(strlen("$xattrs.")));
          return std::make_shared<XAttributeDirectory>(c);
        }
        _fetch();
        auto it = _files.find(name);
        auto self = std::dynamic_pointer_cast<Directory>(shared_from_this());
        if (it != _files.end())
        {
          switch(it->second.first)
          {
          case EntryType::symlink:
            return std::shared_ptr<rfs::Path>(new Symlink(self, _owner, name));
          case EntryType::file:
            return std::shared_ptr<rfs::Path>(new File(self, _owner, name));
          case EntryType::directory:
            return std::shared_ptr<rfs::Path>(new Directory(self, _owner, name,
                it->second.second));
          default:
            return {};
          }
        }
        else
          return std::shared_ptr<rfs::Path>(new Unknown(self, _owner, name));
      }

    struct PrefetchEntry
    {
      Address address;
      int level;
      bool is_dir;
    };

    void
    Directory::_prefetch()
    {
      static int prefetch_threads = std::stoi(
        elle::os::getenv("INFINIT_PREFETCH_THREADS", "3"));
      static int prefetch_depth = std::stoi(
        elle::os::getenv("INFINIT_PREFETCH_DEPTH", "2"));
      int nthreads = std::min(prefetch_threads, signed(_files.size()) / 2);
      if (_prefetching || !nthreads)
        return;
      auto files = std::make_shared<std::vector<PrefetchEntry>>();
      for (auto const& f: _files)
        files->push_back(PrefetchEntry{ f.second.second, 0,
                           f.second.first == EntryType::directory});
      _prefetching = true;
      auto self = std::dynamic_pointer_cast<Directory>(shared_from_this());
      auto model = _owner.block_store();
      auto running = std::make_shared<int>(nthreads);
      for (int i=0; i<nthreads; ++i)
        new reactor::Thread("prefetcher", [self, files, model, running] {
            int nf = 0;
            while (!files->empty())
            {
              ++nf;
              auto e = files->back();
              files->pop_back();
              std::unique_ptr<model::blocks::Block> block;
              try
              {
                block = model->fetch(e.address);
                if (block && e.is_dir && e.level +1 < prefetch_depth)
                {
                  Directory d(self, self->_owner, "", e.address);
                  d._fetch(elle::cast<ACLBlock>::runtime(block));
                  for (auto const& f: d._files)
                    files->push_back(PrefetchEntry{ f.second.second, e.level+1,
                      f.second.first == EntryType::directory});
                }
              }
              catch(elle::Error const& e)
              {
                ELLE_TRACE("Exception while prefeching: %s", e.what());
              }
            }
            if (!(--(*running)))
              self->_prefetching = false;
        }, true);
    }

    void
      Directory::list_directory(rfs::OnDirectoryEntry cb)
      {
        ELLE_TRACE_SCOPE("%s: list", *this);
        _fetch();
        _prefetch();
        struct stat st;
        for (auto const& e: _files)
        {
          switch(e.second.first)
          {
          case EntryType::file:
            st.st_mode = S_IFREG;
            break;
          case EntryType::directory:
            st.st_mode = S_IFDIR;
            break;
          case EntryType::symlink:
            st.st_mode = S_IFLNK;
            break;
          }
          st.st_mode |= 00644;
          st.st_size  = 0;
          st.st_atime = 0;
          st.st_mtime = 0;
          st.st_ctime = 0;
          cb(e.first, &st);
        }
      }

    void
      Directory::rmdir()
      {
        ELLE_TRACE_SCOPE("%s: remove", *this);
        _fetch();
        if (!_files.empty())
          throw rfs::Error(ENOTEMPTY, "Directory not empty");
        if (_parent.get() == nullptr)
          throw rfs::Error(EINVAL, "Cannot delete root node");
        _parent->_files.erase(_name);
        _parent->_commit({OperationType::remove, _name});
        umbrella([&] {_owner.block_store()->remove(_block->address());});
        _remove_from_cache();
      }

    void
      Directory::move_recurse(boost::filesystem::path const& current,
          boost::filesystem::path const& where)
      {
        for (auto const& v: _files)
        {
          std::string const& name = v.first;
          ELLE_DEBUG("Extracting %s", current / name);
          auto p = _owner.filesystem()->extract((current / name).string());
          if (p)
          {
            auto ptr = p.get();
            ELLE_DEBUG("Inserting %s", where / name);
            _owner.filesystem()->set((where/name).string(), std::move(p));
            if (v.second.first == EntryType::directory)
            {
              dynamic_cast<Directory*>(ptr)->move_recurse(current / name, where / name);
            }
          }
        }
      }

    void
      Directory::rename(boost::filesystem::path const& where)
      {
        boost::filesystem::path current = full_path();
        Node::rename(where);
        // We might have children that pointed to us, we need to move them
        this->move_recurse(current, where);
      }

    void
    Directory::stat(struct stat* st)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      bool can_access = false;
      try
      {
        this->_fetch();
        this->Node::stat(st);
        st->st_mode |= S_IFDIR;
        std::pair<bool, bool> perms = _owner.get_permissions(*_block);
        if (!perms.first)
          st->st_mode &= ~0400;
        if (!perms.second)
          st->st_mode &= ~0200;
        if (perms.first)
          st->st_mode |= 0100; // Set x.
        can_access = true;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
      }
      catch (rfs::Error const& e)
      {
        ELLE_DEBUG("%s: fetch exception %s (isaccess=%s)", *this, e.what(),
                   e.error_code() == EACCES);
        if (e.error_code() != EACCES)
          throw;
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("unexpected exception on stat: %s", e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      if (!can_access)
      {
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IFDIR;
        st->st_nlink = 1;
        st->st_dev = 1;
        st->st_ino = (unsigned short)(uint64_t)(void*)this;
      }
    }

    void
    Directory::cache_stats(CacheStats& cs)
    {
      cs.directories++;
      boost::filesystem::path current = full_path();
      for(auto const& f: _files)
      {
        auto p = _owner.filesystem()->get((current / f.first).string());
        if (!p)
          return;
        if (Directory* d = dynamic_cast<Directory*>(p.get()))
          d->cache_stats(cs);
        else if (File* f = dynamic_cast<File*>(p.get()))
        {
          cs.files++;
          cs.blocks += 1 + f->_blocks.size();
          if (f->_first_block)
            cs.size += f->_first_block->data().size();
          for (auto& b: f->_blocks)
            cs.size += b.second.block.data().size();
        }
      }
    }

    void
    Directory::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    Directory::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void Directory::removexattr(std::string const& k)
    {
      Node::removexattr(k);
    }

    void
    Directory::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    std::vector<std::string> Directory::listxattr()
    {
      ELLE_TRACE("directory listxattr");
      _fetch();
      std::vector<std::string> res;
      for (auto const& a: _header.xattrs)
        res.push_back(a.first);
      return res;
    }

    void Directory::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("directory setxattr %s", name);
      _fetch();
      if (name == "user.infinit.auth.inherit")
      {
        bool on = !(value == "0" || value == "false" || value=="");
        _inherit_auth = on;
        _commit({OperationType::update, on ? "/inherit" : "/disinherit"});
      }
      else if (name.find("user.infinit.auth.") == 0)
      {
        set_permissions(name.substr(strlen("user.infinit.auth.")), value,
                        _block->address());
        _block.reset();
      }
      else if (name == "user.infinit.fsck.deref")
      {
        _files.erase(value);
        _commit({OperationType::remove, value}, true);
      }
      else if (name == "user.infinit.fsck.ref")
      {
        auto p1 = value.find_first_of(':');
        auto p2 = value.find_last_of(':');
        if (p1 == p2 || p1 != 1)
          THROW_INVAL;
        EntryType type;
        if (value[0] == 'd')
          type = EntryType::directory;
        else if (value[0] == 'f')
          type = EntryType::file;
        else
          type = EntryType::symlink;
        std::string ename = value.substr(p1+1, p2 - p1 - 1);
        Address eaddr = Address::from_string(value.substr(p2+1));
        _files[ename] = std::make_pair(type, eaddr);
        _commit({OperationType::insert, ename}, true);
      }
      else if (name == "user.infinit.fsck.unlink")
      {
        auto it = _files.find(value);
        if (it == _files.end())
          THROW_NOENT;
        auto c = child(value);
        auto f = dynamic_cast<File*>(c.get());
        if (!f)
          THROW_ISDIR;
        try
        {
          f->unlink();
        }
        catch(std::exception const& e)
        {
          ELLE_WARN("%s: unlink of %s failed with %s, forcibly remove from parent",
                    *this, value, e.what());
          _files.erase(value);
          _commit({OperationType::remove, value}, true);
        }
      }
      else
        Node::setxattr(name, value, flags);
    }

    static std::string perms_to_json(model::Model& model, ACLBlock& block)
    {
      auto perms = block.list_permissions(model);
      elle::json::Array v;
      for (auto const& perm: perms)
      {
        elle::json::Object o;
        o["name"] = perm.user->name();
        o["read"] = perm.read;
        o["write"] = perm.write;
        v.push_back(o);
      }
      std::stringstream ss;
      elle::json::write(ss, v, true);
      return ss.str();
    }

    std::string
    Directory::getxattr(std::string const& key)
    {
      if (key == "user.infinit.block")
      {
        if (_block)
          return elle::sprintf("%x", _block->address());
        else if (_parent)
        {
          auto const& elem = _parent->_files.at(_name);
          return elle::sprintf("%x", elem.second);
        }
        else
          return "<ROOT>";
      }
      else if (key == "user.infinit.auth")
      {
        _fetch();
        return perms_to_json(*_owner.block_store(), *_block);
      }
      else if (key == "user.infinit.auth.inherit")
      {
        _fetch();
        return _inherit_auth ? "true" : "false";
      }
      else if (key == "user.infinit.sync")
      {
        auto dn = std::dynamic_pointer_cast<model::doughnut::Doughnut>(_owner.block_store());
        auto c = dn->consensus().get();
        auto a = dynamic_cast<model::doughnut::consensus::Async*>(c);
        if (!a)
        {
          auto cache = dynamic_cast<model::doughnut::consensus::Cache*>(c);
          if (!cache)
            return "no async";
          a = dynamic_cast<model::doughnut::consensus::Async*>(
            cache->backend().get());
          if (!a)
            return "no async behind cache";
        }
        a->sync();
        return "ok";
      }
      else
        return Node::getxattr(key);
    }

    void
    Directory::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Directory(\"/%s\")", this->_name);
    }
  }
}
