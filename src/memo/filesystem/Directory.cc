#include <memo/filesystem/Directory.hh>

#include <unordered_map>
#include <utility>

#include <boost/algorithm/string/predicate.hpp>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <elle/reactor/exception.hh>

#include <memo/filesystem/Node.hh>
#include <memo/filesystem/File.hh>
#include <memo/filesystem/Symlink.hh>
#include <memo/filesystem/Unknown.hh>
#include <memo/filesystem/xattribute.hh>

#include <memo/model/doughnut/Async.hh>
#include <memo/model/doughnut/Cache.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/User.hh>


#ifdef ELLE_WINDOWS
# undef stat
#endif

ELLE_LOG_COMPONENT("memo.filesystem.Directory");

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<memo::filesystem::EntryType>
    {
      using Type = int;
      static int convert(memo::filesystem::EntryType& et)
      {
        return (int)et;
      }
      static memo::filesystem::EntryType convert(int repr)
      {
        return (memo::filesystem::EntryType)repr;
      }
    };
  }
}

namespace memo
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
                               model::Model& model,
                               Operation op,
                               Address address,
                               bool deserialized)
    {
       ELLE_TRACE("edit conflict on %s (%s %s)",
                  b.address(), op.type, op.target);
       auto d = DirectoryData({}, current, {true, true});
       switch(op.type)
       {
       case OperationType::insert:
       case OperationType::insert_exclusive:
         ELLE_ASSERT(!op.target.empty());
         if (d.files().find(op.target) != d.files().end())
         {
           ELLE_LOG("Conflict: the object %s was also created remotely,"
             " your changes will overwrite the previous content.",
             op.target);
           if (op.type == OperationType::insert_exclusive)
           {
             if (deserialized)
               ELLE_WARN("Ignoring 'exclusive' create flag in asynchronous mode");
             else
               THROW_EXIST();
           }
         }
         ELLE_TRACE("insert: Overriding entry %s", op.target);
         d._files[op.target] = std::make_pair(op.entry_type, op.address);
         break;

       case OperationType::update:
         if (op.target == "")
         {
           ELLE_LOG("Conflict: the directory %s was updated remotely, your"
                    " changes will be dropped.", "");
         }
         else if (op.target == "/perms")
         {
           auto wp = dynamic_cast<ACLBlock&>(b).get_world_permissions();
           auto& aclb = dynamic_cast<ACLBlock&>(current);
           aclb.set_world_permissions(wp.first, wp.second);
         }
         else if (op.target == "/inherit")
         {
           d._inherit_auth = true;
         }
         else if (op.target == "/disinherit")
         {
           d._inherit_auth = false;
         }
         else if (d._files.find(op.target) == d._files.end())
         {
           ELLE_LOG("Conflict: the object %s (%s / %s) was removed remotely,"
             " your changes will be dropped.",
             op.target, "", op.target);
           // FIXME update cached entry
         }
         else if (d._files[op.target].second != op.address)
         {
           ELLE_LOG("Conflict: the object %s was replaced remotely,"
             " your changes will be dropped.",
             op.target);
           // FIXME update cached entry
         }
         else
         {
           ELLE_TRACE("update: Overriding entry %s", op.target);
           d._files[op.target] = std::make_pair(op.entry_type, op.address);
         }
         break;

       case OperationType::remove:
         d._files.erase(op.target);
         break;
       }
       elle::Buffer data = [&d]
         {
           elle::Buffer res;
           elle::IOStream os(res.ostreambuf());
           elle::serialization::binary::SerializerOut output(os);
           output.serialize_forward(d);
           return res;
         }();
       auto res = elle::cast<ACLBlock>::runtime(current.clone());
       res->data(data);
       return std::move(res);
    }

    DirectoryConflictResolver::DirectoryConflictResolver(
      elle::serialization::SerializerIn& s,
      elle::Version const& v)
      : _model(nullptr)
      , _deserialized(true)
    {
      serialize(s, v);
    }

    DirectoryConflictResolver::DirectoryConflictResolver(DirectoryConflictResolver&& b)
      : _model(b._model)
      , _op(b._op)
      , _address(b._address)
      , _deserialized(b._deserialized)
    {}

    DirectoryConflictResolver::DirectoryConflictResolver()
      : _model(nullptr)
      , _deserialized(false)
    {}

    DirectoryConflictResolver::DirectoryConflictResolver(model::Model& model,
                              Operation op,
                              Address address)
      : _model(&model)
      , _op(op)
      , _address(address)
      , _deserialized(false)
    {}

    DirectoryConflictResolver::~DirectoryConflictResolver()
    {}

    std::unique_ptr<Block>
    DirectoryConflictResolver::operator() (Block& block,
                                           Block& current)
    {
      return resolve_directory_conflict(
        block, current,
        *this->_model, this->_op, this->_address, this->_deserialized);
    }

    void
    DirectoryConflictResolver::serialize(elle::serialization::Serializer& s,
                                         elle::Version const& version)
    {
      if (s.in())
        this->_deserialized = true;
      std::string path;
      s.serialize("path", path); // for backward compatibility
      s.serialize("optype", _op.type, elle::serialization::as<int>());
      s.serialize("optarget", _op.target);
      s.serialize("opaddr", _op.address);
      s.serialize("opetype", _op.entry_type, elle::serialization::as<int>());
    }

    struct ConflictContent
    {
      bool pre_addfile;
      bool post_addfile;
      bool same_file;
      bool other_op;
      bool error;
    };

    static
    ConflictContent
    extract_stack_content(model::ConflictResolver::SquashStack const& stack,
                          std::string const& tgt)
    {
      ConflictContent res{false, false, false, false, false};
      for (auto const& c: stack)
      {
        auto const* d = dynamic_cast<DirectoryConflictResolver*>(c.get());
        if (!d)
        {
          res.error = true;
          return res;
        }
        if (d->_op.type != OperationType::insert
          && d->_op.type != OperationType::insert_exclusive)
        {
          res.other_op = true;
          continue;
        }
        if (d->_op.target == tgt)
          res.same_file = true;
        if (d->_op.entry_type == EntryType::pending)
          res.pre_addfile = true;
        else
          res.post_addfile = true;
      }
      return res;
    }

    model::SquashOperation
    DirectoryConflictResolver::squashable(SquashStack const& newops)
    {
      if (this->_op.type != OperationType::insert
        && this->_op.type != OperationType::insert_exclusive)
        return {model::Squash::stop, {}};
      ConflictContent content = extract_stack_content(newops, this->_op.target);
      bool self_pre = this->_op.entry_type == EntryType::pending
        ||this->_op.entry_type == EntryType::directory;
      if (content.other_op || content.same_file || content.error)
        return {model::Squash::stop, {}};
      if (self_pre)
      { // at_first is allowed
        return {model::Squash::at_first_position_continue, {}};
      }
      else
      { // at_first forbiden or we might cross our associated file update block
        if (content.pre_addfile)
        { // we cant't move that block up
          return {model::Squash::skip, {}};
        }
        else
          return {model::Squash::at_last_position_stop, {}};
      }
    }

    std::string
    DirectoryConflictResolver::description() const
    {
      return elle::sprintf("edit directory: %s %s \"%s\"",
                           this->_op.type,
                           this->_op.entry_type,
                           this->_op.target);
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

    DirectoryData::DirectoryData(bfs::path path, Address address)
      : _address{address}
      , _block_version{-1}
      , _inherit_auth{false}
      , _prefetching{false}
      , _last_used{FileSystem::now()}
      , _path{path}
    {}

    DirectoryData::DirectoryData(bfs::path path,
                                 model::blocks::Block& block,
                                 std::pair<bool, bool> perms)
      : DirectoryData{path,
                      Address{block.address().value(),
                              model::flags::mutable_block,
                              false}}
    {
      update(block, perms);
    }

    DirectoryData::DirectoryData(elle::serialization::Serializer& s,
                                 elle::Version const& v)
    {
      this->serialize(s, v);
    }

    void
    DirectoryData::serialize(elle::serialization::Serializer& s,
                             elle::Version const& v)
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
      auto new_version =
        dynamic_cast<model::blocks::MutableBlock&>(block).version();
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
        auto now = time(nullptr);
        _header = FileHeader(0, 1, S_IFDIR | 0600,
                             now, now, now, now,
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
        catch (elle::serialization::Error const& e)
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
    DirectoryData::write(FileSystem& fs,
                         Operation op,
                         std::unique_ptr<model::blocks::ACLBlock>&block,
                         bool set_mtime,
                         bool first_write)
    {
      model::Model& model = *fs.block_store();
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
        auto version = model.version();
        auto versions =
          elle::serialization::_details::dependencies<typename FileData::serialization_tag>(
            version, 42);
        versions.emplace(
          elle::type_info<typename FileData::serialization_tag>(),
          version);
        elle::serialization::binary::SerializerOut output(os, versions, true);
        output.serialize_forward(*this);
      }
      try
      {
        int version = 0;
        auto resolver =
          std::make_unique<DirectoryConflictResolver>(model, op, _address);
        if (block)
        {
          block->data(data);
          version = block->version();
          if (first_write)
            model.seal_and_insert(*block, std::move(resolver));
          else
            model.seal_and_update(*block, std::move(resolver));
        }
        else
        {
          auto b = elle::cast<ACLBlock>::runtime(model.fetch(_address));
          if (b->version() != _block_version)
          {
            ELLE_TRACE("Conflict: block version not expected: %s vs %s",
                     b->version(), _block_version);
            DirectoryConflictResolver dcr(model, op, _address);
            auto nb = dcr(*b, *b);
            b = elle::cast<ACLBlock>::runtime(nb);
            // Update this with the conflict resolved data
            update(*b, get_permissions(model, *b));
          }
          else
            b->data(data);
          version = b->version();
          if (first_write)
            model.insert(std::move(b), std::move(resolver));
          else
            model.update(std::move(b), std::move(resolver));
        }
        ELLE_TRACE("stored version %s of %f", version, _address);
        _block_version = version + 1;
      }
      catch (memo::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        // Evict entry from cache since we put invalid changes there
        fs._directory_cache.erase(_address);
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch (rfs::Error const& e)
      {
        ELLE_TRACE("filesystem error storing %x: %s",
                  _address, e);
        throw;
      }
      catch (elle::Error const& e)
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
        if (auto& storage = dht->local()->storage())
          {
            if (storage->capacity())
              st->f_blocks = *storage->capacity() / 32768;
            st->f_bavail = st->f_bfree = st->f_blocks - storage->usage() / 32768;
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
      _data->write(_owner, op, _block, set_mtime);
    }

    std::shared_ptr<rfs::Path>
    Directory::child(std::string const& name)
    {
      // Never called by rfs::FileSystem, but used in some tests.
      return _owner.path((_data->_path / name).string());
    }

    struct PrefetchEntry
    {
      PrefetchEntry(std::string name, Address address,
                    int level, bool is_dir,
                    boost::optional<int> cached_version)
        : name{std::move(name)}
        , address{std::move(address)}
        , level{level}
        , is_dir{is_dir}
        , cached_version{std::move(cached_version)}
      {}

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
      return {};
    }

    void
    DirectoryData::_prefetch(FileSystem& fs,
                             std::shared_ptr<DirectoryData> self)
    {
      ELLE_ASSERT_EQ(self.get(), this);
      static int prefetch_threads = memo::getenv("PREFETCH_THREADS", 3);
      static int prefetch_depth = memo::getenv("PREFETCH_DEPTH", 2);
      static int prefetch_group = memo::getenv("PREFETCH_GROUP", 5);
      static int prefetch_tasks = memo::getenv("PREFETCH_TASKS", 5);
      // Disable prefetching if we have no cache
      static bool have_cache =
        model::doughnut::consensus::StackedConsensus::find<
          model::doughnut::consensus::Cache>(
            dynamic_cast<model::doughnut::Doughnut&>(
              *fs.block_store()).consensus().get());
      if (prefetch_threads && !have_cache)
      {
        ELLE_TRACE("Disabling directory prefetching since cache is disabled.");
        prefetch_threads = 0;
      }
      int group_size = prefetch_group;
      int nthreads = prefetch_threads;
      if (this->_prefetching ||
          nthreads == 0 ||
          (FileSystem::now() - this->_last_prefetch) < 15s ||
          fs.prefetching() >= prefetch_tasks)
        return;
      this->_last_prefetch = FileSystem::now();
      fs.prefetching()++;
      auto files = std::make_shared<std::vector<PrefetchEntry>>();
      for (auto const& f: this->_files)
        files->emplace_back(
          f.first, f.second.second, 0,
          f.second.first == EntryType::directory,
          cached_version(fs, f.second.second, f.second.first));
      this->_prefetching = true;
      auto running = std::make_shared<int>(nthreads);
      auto parked = std::make_shared<int>(0);
      auto available = std::make_shared<elle::reactor::Barrier>("files_prefetchable");
      if (!files->empty())
        available->open();
      auto prefetch_task =
        [self, files, fs = &fs, running,
         parked, nthreads, group_size, available]
        {
          static elle::Bench bench("bench.fs.prefetch", 10000s);
          elle::Bench::BenchScope bs(bench);
          auto start_time = std::chrono::steady_clock::now();
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
              elle::reactor::wait(*available);
              --*parked;
            }
            if (should_exit)
              break;
            auto addresses = std::vector<model::Model::AddressVersion>{};
            auto recurse =std::unordered_map<Address, int>{};
            do
            {
              ++nf;
              auto e = files->back();
              ELLE_TRACE_SCOPE("%s: prefetch \"%s\"", *self, e.name);
              files->pop_back();
              Address addr(e.address.value(),
                model::flags::mutable_block, false);
              addresses.emplace_back(addr, e.cached_version);
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
                    files->emplace_back(
                      f.first, f.second.second, recurse.at(addr) + 1,
                      f.second.first == EntryType::directory,
                      cached_version(*fs, f.second.second, f.second.first));
                    available->open();
                  }
                }
              }
              catch (elle::Error const& e)
              {
                ELLE_TRACE("Exception while prefeching: %s", e.what());
              }
              catch (std::out_of_range const& e)
              {
                ELLE_TRACE("Entry vanished from cache: %s", e.what());
              }
            }
            else
            { // multifetch
              // FIXME: pass local versions
              fs->block_store()->multifetch(addresses,
                  [&](Address addr, std::unique_ptr<model::blocks::Block> block,
                      std::exception_ptr exception)
                  {
                    if (recurse.find(addr) != recurse.end()
                        && recurse.at(addr) + 1 < prefetch_depth)
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
                            elle::err("directory at %f vanished from cache", addr);
                          d = *it;
                        }
                        for (auto const& f: d->_files)
                          files->emplace_back(
                            f.first, f.second.second, recurse.at(addr) + 1,
                            f.second.first == EntryType::directory,
                            cached_version(*fs, f.second.second, f.second.first));
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
          ELLE_TRACE("prefetched %s entries in %s",
                     nf, std::chrono::steady_clock::now() - start_time);
          if (!--*running)
          {
            self->_prefetching = false;
            fs->prefetching()--;
          }
          auto* self = elle::reactor::scheduler().current();
          auto& running = fs->running();
          auto it = std::find_if(running.begin(), running.end(),
            [self](elle::reactor::Thread::unique_ptr const& p)
            {
              return p.get() == self;
            });
          if (it != running.end())
          {
            (*it)->dispose(true);
            it->release();
            std::swap(running.back(), *it);
            running.pop_back();
          }
          else
            ELLE_WARN("Thread %s not found in running list", self);
      };
      for (int i = 0; i < nthreads; ++i)
        fs.running().emplace_back(
          new elle::reactor::Thread(
            elle::sprintf("prefetcher %x-%x-%s", (void*)&fs, (void*)parked.get(), i),
            prefetch_task
            ));
    }

    void
    Directory::list_directory(rfs::OnDirectoryEntry cb)
    {
      ELLE_TRACE_SCOPE("%s: list", *this);
      _data->_prefetch(_owner, _data);
      struct stat st;
      st.st_size  = 0;
      st.st_atime = 0;
      st.st_mtime = 0;
      st.st_ctime = 0;
      st.st_mode = S_IFDIR | 00644;
      cb(".", &st);
      cb("..", &st);
      for (auto const& e: _data->_files)
      {
        switch(e.second.first)
        {
        case EntryType::pending:
          continue;
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
        THROW_ACCES();
      _parent->_files.erase(_name);
      _parent->write(_owner, {OperationType::remove, _name});
      umbrella([&] {_owner.block_store()->remove(_data->address());});
    }

    void
    Directory::rename(bfs::path const& where)
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
      catch (memo::model::doughnut::ValidationFailed const& e)
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
    Directory::_header_block(bool force)
    {
      if (!_block && force)
      {
        this->_fetch();
        ELLE_ASSERT(_block);
      }
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

    std::vector<std::string>
    Directory::listxattr()
    {
      ELLE_TRACE_SCOPE("%s: listxattr", *this);
      return elle::make_vector(_data->_header.xattrs,
                               [](auto const& a) { return a.first; });
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
          bool on = !(value == "0" || value == "false" || value == "");
          this->_data->_inherit_auth = on;
          this->_data->write(
            _owner,
            {OperationType::update, on ? "/inherit" : "/disinherit"});
          return;
        }
        else if (*special == "fsck.deref")
        {
          this->_data->_files.erase(value);
          this->_data->write(_owner,
                             {OperationType::remove, value},
                             DirectoryData::null_block,
                             true);
          return;
        }
        else if (*special == "fsck.ref")
        {
          auto p1 = value.find_first_of(':');
          auto p2 = value.find_last_of(':');
          if (p1 == p2 || p1 != 1)
            THROW_INVAL();
          auto const type = [&]
            {
              if (value[0] == 'd')
                return EntryType::directory;
              else if (value[0] == 'f')
                return EntryType::file;
              else
                return EntryType::symlink;
            }();
          std::string ename = value.substr(p1 + 1, p2 - p1 - 1);
          Address eaddr = Address::from_string(value.substr(p2 + 1));
          this->_data->_files[ename] = std::make_pair(type, eaddr);
          this->_data->write(_owner,
                             {OperationType::insert, ename},
                             DirectoryData::null_block,
                             true);
          return;
        }
        else if (*special == "fsck.rmblock")
        {
          umbrella([&] {
              this->_owner.block_store()->remove(Address::from_string(value));
          });
          return;
        }
        else if (*special == "fsck.unlink")
        {
          auto it = _data->_files.find(value);
          if (it == _data->_files.end())
            THROW_NOENT();
          File f(_owner, it->second.second, {}, _data, value);
          try
          {
            f.unlink();
          }
          catch (std::exception const& e)
          {
            ELLE_WARN(
              "%s: unlink of %s failed with %s, forcibly remove from parent",
              *this, value, e.what());
            this->_data->_files.erase(value);
            this->_data->write(_owner,
                               Operation{OperationType::remove, value},
                               DirectoryData::null_block, true);
          }
          return;
        }
      }
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
              return this->perms_to_json(*block);
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
            else if (boost::starts_with(*special, "blockof."))
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
