#include <infinit/filesystem/Directory.hh>

#include <unordered_map>
#include <utility>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <reactor/exception.hh>

#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>
#include <infinit/filesystem/xattribute.hh>

#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/User.hh>


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
    FileSystem::clock::time_point
    FileSystem::now()
    {
      return FileSystem::clock::now();
    }
    std::unique_ptr<Block>
    resolve_directory_conflict(Block& b,
                               Block& current,
                               model::StoreMode store_mode,
                               model::Model& model,
                               Operation op,
                               Address address)
    {
       ELLE_TRACE("edit conflict on %s (%s %s)",
                  b.address(), op.type, op.target);
       DirectoryData d({}, current, {true, true});
       switch(op.type)
       {
       case OperationType::insert:
         ELLE_ASSERT(!op.target.empty());
         if (d.files().find(op.target) != d.files().end())
         {
           ELLE_LOG("Conflict: the object %s was also created remotely,"
             " your changes will overwrite the previous content.",
             op.target);
         }
         ELLE_TRACE("insert: Overriding entry %s", op.target);
         d._files[op.target] = std::make_pair(op.entry_type, op.address);
         break;
       case OperationType::update:
         if (op.target == "")
         {
           ELLE_LOG("Conflict: the directory %s was updated remotely, your"
                    " changes will be dropped.", "");
           break;
         }
         else if (op.target == "/perms")
         {
           auto wp = dynamic_cast<ACLBlock&>(b).get_world_permissions();
           dynamic_cast<ACLBlock&>(current).set_world_permissions(wp.first, wp.second);
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
             op.target, "", op.target);
           // FIXME update cached entry
           break;
         }
         else if (d._files[op.target].second != op.address)
         {
           ELLE_LOG("Conflict: the object %s was replaced remotely,"
             " your changes will be dropped.",
             op.target);
           // FIXME update cached entry
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
       auto res = elle::cast<ACLBlock>::runtime(current.clone());
       res->data(data);
       return std::move(res);
    }

    DirectoryConflictResolver::DirectoryConflictResolver(
      elle::serialization::SerializerIn& s,
      elle::Version const& v)
      : _model(nullptr)
    {
      serialize(s, v);
    }

    DirectoryConflictResolver::DirectoryConflictResolver(DirectoryConflictResolver&& b)
      : _model(b._model)
      , _op(b._op)
      , _address(b._address)
    {}

    DirectoryConflictResolver::DirectoryConflictResolver()
      : _model(nullptr)
    {}

    DirectoryConflictResolver::DirectoryConflictResolver(model::Model& model,
                              Operation op,
                              Address address)
      : _model(&model)
      , _op(op)
      , _address(address)
    {}

    DirectoryConflictResolver::~DirectoryConflictResolver()
    {}

    std::unique_ptr<Block>
    DirectoryConflictResolver::operator() (Block& block,
                Block& current,
                model::StoreMode mode)
    {
      return resolve_directory_conflict(
        block, current, mode,
        *this->_model, this->_op, this->_address);
    }

    void
    DirectoryConflictResolver::serialize(elle::serialization::Serializer& s,
                                         elle::Version const& version)
    {
      std::string path;
      s.serialize("path", path); // for backward compatibility
      s.serialize("optype", _op.type, elle::serialization::as<int>());
      s.serialize("optarget", _op.target);
      s.serialize("opaddr", _op.address);
      s.serialize("opetype", _op.entry_type, elle::serialization::as<int>());
    }

    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<DirectoryConflictResolver> _register_dcr("dcr");


    Directory::Directory(FileSystem& owner,
                         std::shared_ptr<DirectoryData> self,
                         std::shared_ptr<DirectoryData> parent,
                         std::string const& name)
      : Node(owner, self->address(), parent, name)
      , _data(self)
    {
      ELLE_TRACE("%s: created with address %f", this, this->_address);
    }

    std::unique_ptr<model::blocks::ACLBlock> DirectoryData::null_block;

    DirectoryData::DirectoryData(boost::filesystem::path path, model::blocks::Block& block, std::pair<bool, bool> perms)
    {
      _path = path;
      _address = Address(block.address().value(), model::flags::mutable_block, false);
      _last_used = FileSystem::now();
      _block_version = -1;
      _prefetching = false;
      update(block, perms);
    }

    DirectoryData::DirectoryData(boost::filesystem::path path, Address address)
    {
      _path = path;
      _address = address;
      _last_used = FileSystem::now();
      _block_version = -1;
      _prefetching = false;
      _inherit_auth = false;
    }

    void
    DirectoryData::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("header", this->_header);
      s.serialize("content", this->_files);
      s.serialize("inherit_auth", this->_inherit_auth);
    }

    static
    std::string
    print_files(DirectoryData::Files const& files)
    {
      std::string res("\n");
      for (auto const& f: files)
      {
        const char* t = (f.second.first == EntryType::file) ? "file" :
          (f.second.first == EntryType::directory) ? "dir " : "sym ";
        res += elle::sprintf("  %15s: %s %f\n", f.first, t, f.second.second);
      }
      return res;
    }

    void
    DirectoryData::update(Block& block, std::pair<bool, bool> perms)
    {
      auto new_version =  dynamic_cast<model::blocks::MutableBlock&>(block).version();
      if (_block_version >= new_version)
      {
        ELLE_WARN("%s: ignoring update at %f from obsolete block %s since we have %s",
                  this, block.address(), new_version, _block_version);
        return;
      }
      ELLE_DEBUG("%s updating from version %s to version %s at %f", this,
                 _block_version,
                 new_version,
                 block.address());
      _last_used = FileSystem::now();

      bool empty = false;
      elle::IOStream is(
        umbrella([&] {
            auto& d = block.data();
            ELLE_DUMP("block data: %s", d);
            empty = d.empty();
            return d.istreambuf();
          }, EACCES));
      if (empty)
      {
        ELLE_DEBUG("block is empty");
        _header = FileHeader(0, 1, S_IFDIR | 0600,
                             time(nullptr), time(nullptr), time(nullptr),
                             File::default_block_size);
        _inherit_auth = false;
      }
      else
      {
        elle::serialization::binary::SerializerIn input(is);
        try
        {
          _files.clear();
          _header.xattrs.clear();
          input.serialize_forward(*this);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("Directory deserialization error: %s", e);
          ELLE_TRACE("%s", elle::Backtrace::current());
          throw rfs::Error(EIO, e.what());
        }
        _header.mode &= ~0606;
        auto& ablock = dynamic_cast<ACLBlock&>(block);
        auto wp = ablock.get_world_permissions();
        if (wp.first)
          _header.mode |= 4;
        if (wp.second)
          _header.mode |= 2;
        if (perms.first)
          _header.mode |= 0400;
        if (perms.second)
          _header.mode |= 0200;
        ELLE_TRACE("Directory block fetch OK");
        ELLE_DUMP("%s", print_files(_files));
      }
      _block_version = dynamic_cast<ACLBlock&>(block).version();
    }

    void
    DirectoryData::write(model::Model& model,
                         Operation op,
                         std::unique_ptr<model::blocks::ACLBlock>&block,
                         bool set_mtime,
                         bool first_write)
    {
      ELLE_DEBUG("%s: write at %s", this, _address);
      ELLE_DUMP("%s", print_files(_files));
      if (set_mtime)
      {
        ELLE_DEBUG_SCOPE("set mtime");
        _header.mtime = time(nullptr);
      }
      elle::Buffer data;
      {
        elle::IOStream os(data.ostreambuf());
        elle::serialization::binary::SerializerOut output(os);
        output.serialize_forward(*this);
      }
      try
      {
        int version = 0;
        if (block)
        {
          block->data(data);
          version = block->version();
          model.store(*block,
            first_write ? model::STORE_INSERT : model::STORE_UPDATE,
            elle::make_unique<DirectoryConflictResolver>(model, op, _address));
        }
        else
        {
          auto b = elle::cast<ACLBlock>::runtime(model.fetch(_address));
          if (b->version() != _block_version)
          {
            ELLE_TRACE("Conflict: block version not expected: %s vs %s",
                     b->version(), _block_version);
            DirectoryConflictResolver dcr(model, op, _address);
            auto nb = dcr(*b, *b, first_write ? model::STORE_INSERT : model::STORE_UPDATE);
            b = elle::cast<ACLBlock>::runtime(nb);
            // Update this with the conflict resolved data
            update(*b, get_permissions(model, *b));
          }
          else
            b->data(data);
          version = b->version();
          model.store(std::move(b),
            first_write ? model::STORE_INSERT : model::STORE_UPDATE,
            elle::make_unique<DirectoryConflictResolver>(model, op, _address));
        }
        ELLE_TRACE("stored version %s of %f", version, _address);
        _block_version = version + 1;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected elle error storing %x: %s",
                  _address, e);
        throw rfs::Error(EIO, e.what());
      }
    }

    FileHeader&
    Directory::_header()
    {
      return _data->_header;
    }

    void
    Directory::_fetch()
    {
      _block = elle::cast<ACLBlock>::runtime(
        _owner.block_store()->fetch(_address));
    }

    void
    Directory::statfs(struct statvfs * st)
    {
      memset(st, 0, sizeof(struct statvfs));
      st->f_bsize = 32768;
      st->f_frsize = 32768;
      st->f_blocks = 1000000000;
      st->f_bavail = 1000000000;
      st->f_bfree = 1000000000;
      st->f_fsid = 1;
      auto dht = std::dynamic_pointer_cast<model::doughnut::Doughnut>(
        this->_owner.block_store());
      if (dht->local())
      {
        auto& storage = dht->local()->storage();
        if (storage)
        {
          if (storage->capacity())
            st->f_blocks = *storage->capacity() / 32768;
          st->f_bavail = st->f_bfree = st->f_blocks - storage->usage() / 32768;
        }
      }
    }

    void
    Directory::_commit(WriteTarget target)
    {
      std::string op;
      if (target == WriteTarget::perms)
        op = "/perms";
      _commit(Operation{OperationType::update, op, EntryType::directory,
              Address::null}, false);
    }

    void
    Directory::_commit(Operation op, bool set_mtime)
    {
      _data->write(*_owner.block_store(), op, _block, set_mtime);
    }

    std::shared_ptr<rfs::Path>
    Directory::child(std::string const& name)
    {
      // Never called by rfs::FileSystem, but used in some tests.
      return _owner.path((_data->_path / name).string());
    }

    struct PrefetchEntry
    {
      std::string name;
      Address address;
      int level;
      bool is_dir;
      boost::optional<int> cached_version;
    };

    static
    boost::optional<int>
    cached_version(FileSystem& fs, Address addr, EntryType type)
    {
      if (type == EntryType::directory)
      {
        auto it = fs.directory_cache().find(addr);
        if (it != fs.directory_cache().end())
          return (*it)->block_version();
      }
      else
      {
        auto it = fs.file_cache().find(addr);
        if (it != fs.file_cache().end())
          return (*it)->block_version();
      }
      return boost::optional<int>();
    }

    void
    DirectoryData::_prefetch(FileSystem& fs,
                             std::shared_ptr<DirectoryData> self)
    {
      ELLE_ASSERT_EQ(self.get(), this);
      static int prefetch_threads = std::stoi(
        elle::os::getenv("INFINIT_PREFETCH_THREADS", "3"));
      static int prefetch_depth = std::stoi(
        elle::os::getenv("INFINIT_PREFETCH_DEPTH", "2"));
      static int prefetch_group = std::stoi(
        elle::os::getenv("INFINIT_PREFETCH_GROUP", "5"));
      int group_size = prefetch_group;
      int nthreads = prefetch_threads;
      if (this->_prefetching ||
          nthreads == 0 ||
          (FileSystem::now() - this->_last_prefetch) < std::chrono::seconds(15))
        return;
      this->_last_prefetch = FileSystem::now();
      auto files = std::make_shared<std::vector<PrefetchEntry>>();
      for (auto const& f: this->_files)
        files->push_back(
          PrefetchEntry{f.first, f.second.second, 0,
                        f.second.first == EntryType::directory,
                        cached_version(fs, f.second.second, f.second.first)
          });
      this->_prefetching = true;
      auto running = std::make_shared<int>(nthreads);
      auto parked = std::make_shared<int>(0);
      auto available = std::make_shared<reactor::Barrier>("files_prefetchable");
      if (!files->empty())
        available->open();
      auto prefetch_task =
        [self, files, fs = &fs, running,
         parked, nthreads, group_size, available]
        {
          static elle::Bench bench("bench.fs.prefetch", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          auto start_time = boost::posix_time::microsec_clock::universal_time();
          int nf = 0;
          bool should_exit = false;
          while (true)
          {
            while (files->empty())
            {
              ELLE_DEBUG("parking");
              ++*parked;
              if (*parked == nthreads)
              {
                ELLE_DEBUG("all threads parked");
                available->open();
                should_exit = true;
                break;
              }
              else
                ELLE_DEBUG("%s/%s threads parked", *parked, nthreads);
              available->close();
              reactor::wait(*available);
              --*parked;
            }
            if (should_exit)
              break;
            std::vector<model::Model::AddressVersion> addresses;
            std::unordered_map<Address, int> recurse;
            do
            {
              ++nf;
              auto e = files->back();
              ELLE_TRACE_SCOPE("%s: prefetch \"%s\"", *self, e.name);
              files->pop_back();
              Address addr(e.address.value(),
                model::flags::mutable_block, false);
              addresses.push_back(std::make_pair(addr, e.cached_version));
              if (e.is_dir && e.level + 1 < prefetch_depth)
                recurse.insert(std::make_pair(addr, e.level));;
            }
            while (signed(addresses.size()) < group_size && !files->empty());
            if (files->empty())
              available->close();
            if (addresses.size() == 1)
            {
              std::unique_ptr<model::blocks::Block> block;
              try
              {
                Address addr = addresses.front().first;
                block = fs->block_store()->fetch(addr, addresses.front().second);
                if (!recurse.empty())
                {
                  std::shared_ptr<DirectoryData> d;
                  if (block)
                    d = std::shared_ptr<DirectoryData>(
                      new DirectoryData({}, *block, {true, true}));
                  else
                    d = *(fs->directory_cache().find(addr));
                  for (auto const& f: d->_files)
                  {
                    files->push_back(
                      PrefetchEntry{f.first, f.second.second, recurse.at(addr)+1,
                                    f.second.first == EntryType::directory,
                                    cached_version(*fs, f.second.second, f.second.first)
                      });
                    available->open();
                  }
                }
              }
              catch(elle::Error const& e)
              {
                ELLE_TRACE("Exception while prefeching: %s", e.what());
              }
              catch(std::out_of_range const& e)
              {
                ELLE_TRACE("Entry vanished from cache: %s", e.what());
              }
            }
            else
            { // multifetch
              // FIXME: pass local versions
              fs->block_store()->fetch(addresses,
                  [&](Address addr, std::unique_ptr<model::blocks::Block> block,
                      std::exception_ptr exception)
                  {
                    if (recurse.find(addr) != recurse.end()
                      && recurse.at(addr) + 1 < prefetch_depth
                      )
                    {
                      try
                      {
                        std::shared_ptr<DirectoryData> d;
                        if (block)
                        d = std::shared_ptr<DirectoryData>(
                          new DirectoryData({}, *block, {true, true}));
                        else
                        {
                          auto it = fs->directory_cache().find(addr);
                          if (it == fs->directory_cache().end())
                            throw elle::Error(
                              elle::sprintf("directory at %f vanished from cache", addr));
                          d = *it;
                        }
                        for (auto const& f: d->_files)
                        files->push_back(
                          PrefetchEntry{f.first, f.second.second, recurse.at(addr) +1,
                              f.second.first == EntryType::directory,
                              cached_version(*fs, f.second.second, f.second.first)
                              });
                        available->open();
                      }
                      catch (elle::Error const& e)
                      {
                        ELLE_TRACE("Exception while prefeching: %s", e.what());
                      }
                    }
                  });
            }
          }
          ELLE_TRACE("prefetched %s entries in %s us",
                   nf,
                   (boost::posix_time::microsec_clock::universal_time() - start_time)
                     .total_microseconds());
          if (!(--(*running)))
            self->_prefetching = false;
          fs->pending().clear();
          auto* self = reactor::scheduler().current();
          auto& running = fs->running();
          auto it = std::find_if(running.begin(), running.end(),
            [self](reactor::Thread::unique_ptr const& p)
            {
              return p.get() == self;
            });
          if (it != running.end())
          {
            fs->pending().emplace_back(std::move(*it));
            std::swap(running.back(), *it);
            running.pop_back();
          }
      };
      for (int i = 0; i < nthreads; ++i)
        fs.running().emplace_back(
          new reactor::Thread(
            elle::sprintf("prefetcher %x-%s", (void*)parked.get(), i),
            prefetch_task
            ));
    }

    void
    Directory::list_directory(rfs::OnDirectoryEntry cb)
    {
      ELLE_TRACE_SCOPE("%s: list", *this);
      _data->_prefetch(_owner, _data);
      struct stat st;
      for (auto const& e: _data->_files)
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
      if (!_data->_files.empty())
        throw rfs::Error(ENOTEMPTY, "Directory not empty");
      if (_parent.get() == nullptr)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      if ( !(_data->_header.mode & 0200)
        || !(_parent->_header.mode & 0200))
        THROW_ACCES;
      _parent->_files.erase(_name);
      _parent->write(*_owner.block_store(), {OperationType::remove, _name});
      umbrella([&] {_owner.block_store()->remove(_data->address());});
    }

    void
    Directory::rename(boost::filesystem::path const& where)
    {
      Node::rename(where);
    }

    void
    Directory::stat(struct stat* st)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      bool can_access = false;
      try
      {
        this->Node::stat(st);
        st->st_mode |= S_IFDIR;
        if (st->st_mode & 0400)
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

    model::blocks::ACLBlock*
    Directory::_header_block()
    {
      return _block.get();
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

    void
    Directory::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    /*--------------------.
    | Extended attributes |
    `--------------------*/

    static
    std::string
    perms_to_json(model::Model& model, ACLBlock& block)
    {
      auto perms = block.list_permissions(model);
      elle::json::Array v;
      for (auto const& perm: perms)
      {
        elle::json::Object o;
        o["admin"] = perm.admin;
        o["name"] = perm.user->name();
        o["owner"] = perm.owner;
        o["read"] = perm.read;
        o["write"] = perm.write;
        v.push_back(o);
      }
      std::stringstream ss;
      elle::json::write(ss, v, true);
      return ss.str();
    }

    std::vector<std::string>
    Directory::listxattr()
    {
      ELLE_TRACE_SCOPE("%s: listxattr", *this);
      std::vector<std::string> res;
      for (auto const& a: _data->_header.xattrs)
        res.push_back(a.first);
      return res;
    }

    void
    Directory::setxattr(std::string const& name,
                        std::string const& value,
                        int flags)
    {
      ELLE_TRACE_SCOPE("%s: setxattr %s", *this, name);
      if (auto special = xattr_special(name))
      {
        ELLE_DEBUG("handle special xattr %s", *special);
        if (*special == "auth.inherit")
        {
          bool on = !(value == "0" || value == "false" || value=="");
          this->_data->_inherit_auth = on;
          this->_data->write(
            *_owner.block_store(),
            {OperationType::update, on ? "/inherit" : "/disinherit"});
        }
        else if (special->find("auth.") == 0)
        {
          auto perms = special->substr(5);
          ELLE_DEBUG("set permissions %s", perms);
          set_permissions(perms, value, this->_data->address());
        }
        else if (special->find("register.") == 0)
        {
          umbrella([&] {
            auto dht = std::dynamic_pointer_cast<model::doughnut::Doughnut>(
              this->_owner.block_store());
            auto name = special->substr(9);
            std::stringstream s(value);
            auto p = elle::serialization::json::deserialize<model::doughnut::Passport>(s, false);
            model::doughnut::UB ub(dht.get(), name, p, false);
            model::doughnut::UB rub(dht.get(), name, p, true);
            this->_owner.block_store()->store(ub,  model::STORE_INSERT, model::make_drop_conflict_resolver());
            this->_owner.block_store()->store(rub, model::STORE_INSERT, model::make_drop_conflict_resolver());
          });
        }
        else if (*special == "fsck.deref")
        {
          this->_data->_files.erase(value);
          this->_data->write(*_owner.block_store(), {OperationType::remove, value}, DirectoryData::null_block,
                             true);
        }
        else if (*special == "fsck.ref")
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
          this->_data->_files[ename] = std::make_pair(type, eaddr);
          this->_data->write(*_owner.block_store(), {OperationType::insert, ename}, DirectoryData::null_block,
                      true);
        }
        else if (*special == "fsck.rmblock")
        {
          umbrella([&] {
              this->_owner.block_store()->remove(Address::from_string(value));
          });
        }
        else if (*special == "fsck.unlink")
        {
          auto it = _data->_files.find(value);
          if (it == _data->_files.end())
            THROW_NOENT;
          File f(_owner, it->second.second, {}, _data, value);
          try
          {
            f.unlink();
          }
          catch(std::exception const& e)
          {
            ELLE_WARN(
              "%s: unlink of %s failed with %s, forcibly remove from parent",
              *this, value, e.what());
            this->_data->_files.erase(value);
            this->_data->write(*_owner.block_store(),
                               Operation{OperationType::remove, value},
                               DirectoryData::null_block, true);
          }
        }
        else if (special->find("group.") == 0)
        {
          auto dht = std::dynamic_pointer_cast<model::doughnut::Doughnut>(
            this->_owner.block_store());
          if (dht->version() < elle::Version(0, 4, 0))
          {
            ELLE_WARN(
              "drop group operation as network version %s is too old "
              "(groups are available from 0.4.0)",
              dht->version());
            THROW_NOSYS;
          }
          special = special->substr(6);
          if (*special == "create")
          {
            model::doughnut::Group g(*dht, value);
            g.create();
          }
          else if (*special == "delete")
          {
            model::doughnut::Group g(*dht, value);
            g.destroy();
          }
          else if (*special == "add")
          {
            auto sep = value.find_first_of(':');
            auto gn = value.substr(0, sep);
            auto userdata = value.substr(sep+1);
            model::doughnut::Group g(*dht, gn);
            g.add_member(elle::Buffer(userdata.data(), userdata.size()));
          }
          else if (*special == "remove")
          {
            auto sep = value.find_first_of(':');
            auto gn = value.substr(0, sep);
            auto userdata = value.substr(sep+1);
            model::doughnut::Group g(*dht, gn);
            g.remove_member(elle::Buffer(userdata.data(), userdata.size()));
          }
          else if (*special == "addadmin")
          {
            auto sep = value.find_first_of(':');
            auto gn = value.substr(0, sep);
            auto userdata = value.substr(sep+1);
            model::doughnut::Group g(*dht, gn);
            g.add_admin(elle::Buffer(userdata.data(), userdata.size()));
          }
          else if (*special == "removeadmin")
          {
            auto sep = value.find_first_of(':');
            auto gn = value.substr(0, sep);
            auto userdata = value.substr(sep+1);
            model::doughnut::Group g(*dht, gn);
            g.remove_admin(elle::Buffer(userdata.data(), userdata.size()));
          }
        }
      }
      else
        Node::setxattr(name, value, flags);
    }

    std::string
    Directory::getxattr(std::string const& key)
    {
      return umbrella(
        [&] () -> std::string
        {
          ELLE_TRACE_SCOPE("%s: getxattr %s", *this, key);
          auto dht = std::dynamic_pointer_cast<model::doughnut::Doughnut>(
            this->_owner.block_store());
          if (auto special = xattr_special(key))
          {
            if (*special == "auth")
            {
              auto block = elle::cast<ACLBlock>::runtime(
                this->_owner.block_store()->fetch(this->_data->address()));
              return perms_to_json(*this->_owner.block_store(), *block);
            }
            else if (*special == "auth.inherit")
            {
              return this->_data->_inherit_auth ? "true" : "false";
            }
            else if (*special == "sync")
            {
              auto c = dht->consensus().get();
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
            else if (special->find("group.control_key.") == 0)
            {
              std::string value = special->substr(strlen("group.control_key."));
              return umbrella(
                [&]
                {
                  model::doughnut::Group g(*dht, value);
                  return elle::serialization::json::serialize(
                    g.public_control_key()).string();
                });
            }
            else if (special->find("group.list.") == 0)
            {
              std::string value = special->substr(strlen("group.list."));
              return umbrella(
                [&]
                {
                  model::doughnut::Group g(*dht, value);
                  elle::json::Object o;
                  auto members = g.list_members();
                  elle::json::Array v;
                  for (auto const& m: members)
                    v.push_back(m->name());
                  o["members"] = v;
                  members = g.list_admins();
                  elle::json::Array va;
                  for (auto const& m: members)
                    va.push_back(m->name());
                  o["admins"] = va;
                  std::stringstream ss;
                  elle::json::write(ss, o, true);
                  return ss.str();
                });
            }
            else if (special->find("blockof.") == 0)
            {
              return umbrella(
                [&]
                {
                  std::string file = special->substr(strlen("blockof."));
                  auto addr = this->_data->files().at(file).second;
                  auto block = this->_owner.block_store()->fetch(addr);
                  return elle::serialization::json::serialize(block).string();
                });
            }
            else if (special->find("resolve.") == 0)
            {
              return umbrella(
                [&]
                {
                  std::string what = special->substr(strlen("resolve."));
                  if (what.empty())
                    THROW_NODATA
                  std::unique_ptr<model::doughnut::User> user;
                  user = std::dynamic_pointer_cast<model::doughnut::User>
                    (dht->make_user(what));
                  if (!user)
                  {
                    auto block = elle::cast<ACLBlock>::runtime(
                      this->_owner.block_store()->fetch(this->_data->address()));
                    for (auto& e: block->list_permissions(*dht))
                    {
                      if (model::doughnut::short_key_hash(
                        dynamic_cast<model::doughnut::User*>(e.user.get())->key())
                          == what)
                      {
                        user = std::dynamic_pointer_cast<model::doughnut::User>
                          (std::move(e.user));
                        break;
                      }
                    }
                  }
                  if (!user)
                    THROW_INVAL;
                  elle::json::Object o;
                  o["name"] = std::string(user->name());
                  auto serkey = elle::serialization::json::serialize(user->key());
                  std::stringstream s(serkey.string());
                  o["key"] = elle::json::read(s);
                  o["key_hash"] = model::doughnut::short_key_hash(user->key());
                  std::stringstream ss;
                  elle::json::write(ss, o, true);
                  return ss.str();
                });
            }
          }
          return Node::getxattr(key);
        });
    }

    void Directory::removexattr(std::string const& k)
    {
      this->Node::removexattr(k);
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Directory::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Directory(\"/%s\")", this->_name);
    }
  }
}
