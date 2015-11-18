#include <infinit/filesystem/Directory.hh>
#include <elle/cast.hh>
#include <reactor/exception.hh>

#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>
#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/xattribute.hh>

#include <elle/serialization/binary.hh>
#include <infinit/model/doughnut/Doughnut.hh>
// #include <infinit/filesystem/FileHandle.hh>

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_WINDOWS
#undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.Directory");

namespace infinit
{
  namespace filesystem
  {
    std::unique_ptr<Block>
    resolve_directory_conflict(Block& b, model::StoreMode store_mode,
                               boost::filesystem::path p,
                               FileSystem& owner,
                               Operation op,
                               FileData fd,
                               std::weak_ptr<Directory> wd)
    {
       ELLE_TRACE("edit conflict on %s (%s %s)",
                  b.address(), op.type, op.target);
       Directory d({}, owner, "", b.address());
       d._fetch();
       switch(op.type)
       {
       case OperationType::insert:
         if (d._files.find(op.target) != d._files.end())
         {
           ELLE_LOG("Conflict: the object %s was also created remotely,"
             " your changes will overwrite the previous content.",
             p / op.target);
         }
         ELLE_TRACE("insert: Overriding entry %s", op.target);
         d._files[op.target] = fd;
         break;
       case OperationType::update:
         if (op.target == "/inherit")
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
         else if (d._files[op.target].address != fd.address)
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
         d._files[op.target] = fd;
         break;
       case OperationType::remove:
         d._files.erase(op.target);
         break;
       }
       elle::Buffer data;
       {
         elle::IOStream os(data.ostreambuf());
         elle::serialization::json::SerializerOut output(os);
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
      DirectoryConflictResolver()
      : _owner(nullptr)
      {}
      DirectoryConflictResolver(boost::filesystem::path p,
                                FileSystem* owner,
                                Operation op,
                                FileData fd,
                                std::weak_ptr<Directory> wd)
        : _path(p)
        , _owner(owner)
        , _owner_allocated(false)
        , _op(op)
        , _fd(fd)
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
      operator() (Block& block, model::StoreMode mode) override
      {
        return resolve_directory_conflict(block, mode, _path, *_owner, _op, _fd,
                                          _wptr);
      }

      void serialize(elle::serialization::Serializer& s) override
      {
        std::string spath = _path.string();
        s.serialize("path", spath);
        _path = spath;
        s.serialize("optype", _op.type, elle::serialization::as<int>());
        s.serialize("optarget", _op.target);
        s.serialize("fd", _fd);
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
      FileData _fd;
      std::weak_ptr<Directory> _wptr;
      typedef infinit::serialization_tag serialization_tag;
    };
    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<DirectoryConflictResolver> _register_dcr("dcr");

    void Directory::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("content", this->_files);
      s.serialize("inherit_auth", this->_inherit_auth);
    }

    Directory::Directory(DirectoryPtr parent, FileSystem& owner,
                         std::string const& name,
                         Address address)
      : Node(owner, parent, name)
      , _address(address)
      , _inherit_auth(_parent?_parent->_inherit_auth : false)
    {}

    void Directory::_fetch()
    {
      ELLE_TRACE_SCOPE("%s: fetch block", *this);
      this->_block = elle::cast<ACLBlock>::runtime(
        this->_owner.fetch_or_die(this->_address));
      ELLE_DUMP("block: %s", *this->_block);
      std::unordered_map<std::string, FileData> local;
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
        return;
      elle::serialization::json::SerializerIn input(is);
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
      Directory::_commit(Operation op, bool set_mtime)
      {
        ELLE_TRACE_SCOPE("%s: commit %s entries", *this, _files.size());
        elle::SafeFinally clean_cache([&] { _block.reset();});
        elle::Buffer data;
        {
          elle::IOStream os(data.ostreambuf());
          elle::serialization::json::SerializerOut output(os);
          output.serialize_forward(*this);
        }
        ELLE_DUMP("content: %s", data);
        if (!_block)
          ELLE_DEBUG("fetch root block")
            _block = elle::cast<ACLBlock>::runtime(_owner.fetch_or_die(_address));
        _block->data(data);
        if (set_mtime && _parent)
        {
          ELLE_DEBUG_SCOPE("set mtime");
          FileData& f = _parent->_files.at(_name);
          f.mtime = time(nullptr);
          f.ctime = time(nullptr);
          f.address = _block->address();
          this->_parent->_commit({OperationType::update, _name});
        }
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
          FileData fd;
          auto it = _files.find(op.target);
          if (it != _files.end())
            fd = it->second;
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
               full_path(), &_owner, op, fd, wptr));
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
          bool isDir = signed(it->second.mode & S_IFMT)  == DIRECTORY_MASK;
          bool isSymlink = signed(it->second.mode & S_IFMT) == SYMLINK_MASK;
          if (isSymlink)
            return std::shared_ptr<rfs::Path>(new Symlink(self, _owner, name));
          if (!isDir)
            return std::shared_ptr<rfs::Path>(new File(self, _owner, name));

          return std::shared_ptr<rfs::Path>(new Directory(self, _owner, name,
                it->second.address));
        }
        else
          return std::shared_ptr<rfs::Path>(new Unknown(self, _owner, name));
      }

    void
      Directory::list_directory(rfs::OnDirectoryEntry cb)
      {
        ELLE_TRACE_SCOPE("%s: list", *this);
        _fetch();
        struct stat st;
        for (auto const& e: _files)
        {
          if (e.first.empty())
            continue;
          st.st_mode = e.second.mode;
          st.st_size = e.second.size;
          st.st_atime = e.second.atime;
          st.st_mtime = e.second.mtime;
          st.st_ctime = e.second.ctime;
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
            if (signed(v.second.mode & S_IFMT) ==  DIRECTORY_MASK)
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
          _fetch();
          can_access = true;
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
        }
        catch (rfs::Error const& e)
        {
          if (e.error_code() != EACCES)
            throw;
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("unexpected exception on stat: %s", e);
          throw rfs::Error(EIO, elle::sprintf("%s", e));
        }
        Node::stat(st);
        if (!can_access)
          st->st_mode &= ~0777;
      }

    void
    Directory::cache_stats(CacheStats& cs)
    {
      cs.directories++;
      boost::filesystem::path current = full_path();
      for(auto const& f: _files)
      {
        auto p = _owner.filesystem()->get((current / f.second.name).string());
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
      std::vector<std::string> res;
      if (!_parent)
      {
        auto it = _files.find("");
        if (it == _files.end())
          return res;
        for (auto const& a: it->second.xattrs)
          res.push_back(a.first);
        return res;
      }
      for (auto const& a: _parent->_files.at(_name).xattrs)
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

    static std::string perms_to_json(ACLBlock& block)
    {
      auto perms = block.list_permissions();
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
          return elle::sprintf("%x", elem.address);
        }
        else
          return "<ROOT>";
      }
      else if (key == "user.infinit.auth")
      {
        _fetch();
        return perms_to_json(*_block);
      }
      else if (key == "user.infinit.auth.inherit")
      {
        _fetch();
        return _inherit_auth ? "true" : "false";
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
