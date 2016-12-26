#include <infinit/filesystem/filesystem.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/xattribute.hh>
#include <infinit/filesystem/Unreachable.hh>

#include <infinit/model/MissingBlock.hh>

#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/unordered_map.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>

#include <elle/reactor/filesystem.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/exception.hh>

#include <elle/cryptography/hash.hh>

#include <infinit/utility.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/serialization.hh>

#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

ELLE_LOG_COMPONENT("infinit.filesystem");

namespace rfs = elle::reactor::filesystem;
namespace dht = infinit::model::doughnut;

namespace infinit
{
  namespace filesystem
  {
    std::ostream&
    operator <<(std::ostream& out, EntryType entry)
    {
      switch (entry)
      {
      case EntryType::pending:
        return out << "pending";
      case EntryType::file:
        return out << "file";
      case EntryType::directory:
        return out << "directory";
      case EntryType::symlink:
        return out << "symlink";
      }
      elle::unreachable();
    }

    std::ostream&
    operator <<(std::ostream& out, OperationType operation)
    {
      switch (operation)
      {
      case OperationType::insert:
        return out << "insert";
      case OperationType::insert_exclusive:
        return out << "insert_exclusive";
      case OperationType::update:
        return out << "update";
      case OperationType::remove:
        return out << "remove";
      }
      elle::unreachable();
    }


    FileSystem::FileSystem(
        std::string volume_name,
        std::shared_ptr<model::Model> model,
        boost::optional<elle::cryptography::rsa::PublicKey> owner,
        boost::optional<bfs::path> root_block_cache_dir,
        boost::optional<bfs::path> mountpoint,
        bool allow_root_creation,
        bool map_other_permissions,
        boost::optional<int> block_size)
      : _block_store(std::move(model))
      , _single_mount(false)
      , _owner(owner)
      , _volume_name(std::move(volume_name))
      , _root_block_cache_dir(root_block_cache_dir)
      , _mountpoint(mountpoint)
      , _root_address(Address::null)
      , _allow_root_creation(allow_root_creation)
      , _map_other_permissions(map_other_permissions)
      , _prefetching(0)
      , _block_size(block_size)
      , _file_buffers()
    {
      auto& dht = dynamic_cast<model::doughnut::Doughnut&>(
        *this->_block_store.get());
      auto passport = dht.passport();
      this->_read_only = !passport.allow_write();
      this->_network_name = passport.network();
    }

    void
    FileSystem::filesystem(elle::reactor::filesystem::FileSystem* fs)
    {
      this->_filesystem = fs;
      fs->full_tree(false);
    }

    elle::reactor::filesystem::FileSystem*
    FileSystem::filesystem()
    {
      return _filesystem;
    }

    FileSystem::~FileSystem()
    {
      ELLE_DEBUG("%s: destroy", this);
      while (!this->_running.empty())
      {
        auto t = std::move(this->_running.back());
        this->_running.pop_back();
        ELLE_DEBUG("terminating %s", *t)
        t->terminate_now();
      }
      ELLE_DEBUG("%s: destroyed", this);
    }

    void
    unchecked_remove(model::Model& model, model::Address address)
    {
      try
      {
        model.remove(address);
      }
      catch (model::MissingBlock const&)
      {
        ELLE_DEBUG("%s: block %f was not published", model, address);
      }
      catch (elle::Exception const& e)
      {
        ELLE_ERR("%s: unexpected exception: %s\n%s", model, e.what(), e.backtrace());
        throw;
      }
      catch (...)
      {
        ELLE_ERR("%s: unknown exception", model);
        throw;
      }
    }

    void
    unchecked_remove_chb(model::Model& model,
                         model::Address chb,
                         model::Address owner)
    {
      try
      {
        model.remove(chb, model::doughnut::CHB::sign_remove(model, chb, owner));
      }
      catch (model::MissingBlock const&)
      {
        ELLE_DEBUG("%s: block %f was not published", model, chb);
      }
      catch (elle::Exception const& e)
      {
        ELLE_ERR("%s: unexpected exception: %s\n%s", model, e.what(), e.backtrace());
        throw;
      }
      catch (...)
      {
        ELLE_ERR("%s: unknown exception", model);
        throw;
      }
    }

    void
    FileSystem::unchecked_remove(model::Address address)
    {
      filesystem::unchecked_remove(*block_store(), address);
    }

    void
    FileSystem::store_or_die(model::blocks::Block& block,
                             bool insert,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      block.seal();
      auto copy = block.clone();
      store_or_die(std::move(copy), insert, std::move(resolver));
    }

    void
    FileSystem::store_or_die(std::unique_ptr<model::blocks::Block> block,
                             bool insert,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store or die: %s", this, block);
      auto address = block->address();
      try
      {
        if (insert)
          this->_block_store->insert(std::move(block), std::move(resolver));
        else
          this->_block_store->update(std::move(block), std::move(resolver));
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch (infinit::storage::InsufficientSpace const& e)
      {
        ELLE_TRACE("store_or_die: %s", e.what());
        THROW_ENOSPC();
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected exception storing %x: %s",
                  address, e);
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::Block>
    fetch_or_die(model::Model& model,
                 model::Address address,
                 boost::optional<int> local_version,
                 bfs::path const& path)
    {
      try
      {
        return model.fetch(address, std::move(local_version));
      }
      catch(elle::reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception fetching %f(%s): %s", address, path, e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("data not found fetching %f(%s): %s",
                  address, path, mb);
        throw rfs::Error(EIO, elle::sprintf("%s", mb));
      }
      catch (elle::serialization::Error const& se)
      {
        ELLE_WARN("serialization error fetching %f(%s): %s", address, path, se);
        throw rfs::Error(EIO, elle::sprintf("%s", se));
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected exception fetching %f(%s): %s", address, path, e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected exception on fetching %f(%s): %s", address, path, e.what());
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::Block>
    FileSystem::fetch_or_die(model::Address address,
                             boost::optional<int> local_version,
                             bfs::path const& path)
    {
      return filesystem::fetch_or_die(*this->block_store(), address,
                                      local_version, path);
    }

    std::unique_ptr<model::blocks::MutableBlock>
    FileSystem::unchecked_fetch(model::Address address)
    {
      try
      {
        return elle::cast<model::blocks::MutableBlock>::runtime
          (_block_store->fetch(address));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("Unexpected storage result: %s", mb);
      }
      return {};
    }

    struct BlockMigration
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;
      BlockMigration(Address const& from,
             Address const& to)
        : Super()
        , _from(from)
        , _to(to)
      {}

      BlockMigration(elle::serialization::Serializer& s,
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
        s.serialize("from", this->_from);
        s.serialize("to", this->_to);
      }

      std::string
      description() const override
      {
        return elle::sprintf("migrate %f to %f", this->_from, this->_to);
      }

      ELLE_ATTRIBUTE(Address, from);
      ELLE_ATTRIBUTE(Address, to);
    };

    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<BlockMigration> _register_BlockMigration_resolver("BlockMigration");

    struct InsertRootBlock
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;
      InsertRootBlock(Address const& address)
        : Super()
        , _address(address)
      {}

      InsertRootBlock(elle::serialization::Serializer& s,
                      elle::Version const& version)
        : Super()
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
        s.serialize("address", this->_address);
      }

      std::string
      description() const override
      {
        return elle::sprintf("insert root block at %f", this->_address);
      }

      ELLE_ATTRIBUTE(Address, address);
    };

    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<InsertRootBlock> _register_InsertRootBlock_resolver("InsertRootBlock");

    struct InsertRootBootstrapBlock
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;
      InsertRootBootstrapBlock(Address const& address)
        : Super()
        , _address(address)
      {}

      InsertRootBootstrapBlock(elle::serialization::Serializer& s,
                               elle::Version const& version)
        : Super()
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
      }

      std::string
      description() const override
      {
        return elle::sprintf("insert bootstrap root block at %f",
                             this->_address);
      }

      ELLE_ATTRIBUTE(Address, address);
    };

    static const elle::serialization::Hierarchy<infinit::model::ConflictResolver>::
    Register<InsertRootBootstrapBlock>
    _register_InsertRootBootstrapBlock_resolver("InsertRootBootstrapBlockResolver");

    Address
    FileSystem::root_address()
    {
      if (this->_root_address != Address::null)
        return this->_root_address;
      boost::optional<bfs::path> root_cache;
      if (this->root_block_cache_dir())
      {
        auto dir = this->root_block_cache_dir().get();
        if (!bfs::exists(dir))
          bfs::create_directories(dir);
        root_cache = dir / "root_block";
        ELLE_DEBUG("root block cache: %s", root_cache);
      }
      bool migrate = true;
      auto dn = std::dynamic_pointer_cast<dht::Doughnut>(this->_block_store);
      auto const bootstrap_name = this->_volume_name + ".root";
      Address bootstrap_addr =
        dht::NB::address(this->owner(), bootstrap_name, dn->version());
      while (true)
      {
        try
        {
          ELLE_DEBUG_SCOPE("fetch root bootstrap block at %f", bootstrap_addr);
          auto block = this->_block_store->fetch(bootstrap_addr);
          this->_root_address = Address(
            Address::from_string(block->data().string()).value(),
            model::flags::mutable_block,
            false);
          if (root_cache && !bfs::exists(*root_cache))
          {
            bfs::ofstream ofs(*root_cache);
            elle::fprintf(ofs, "%x", this->_root_address);
          }
          return this->_root_address;
        }
        catch (model::MissingBlock const& e)
        {
          if (migrate)
            try
            {
              migrate = false;
              Address old_addr = dht::NB::address(
                *dn->owner(), bootstrap_name, elle::Version(0, 4, 0));
              auto old = std::dynamic_pointer_cast<dht::NB>(
                this->_block_store->fetch(old_addr));
              ELLE_LOG_SCOPE(
                "migrate old bootstrap block from %s to %s",
                old_addr, bootstrap_addr);
              auto nb = std::make_unique<dht::NB>(
                *dn, dn->owner(), bootstrap_name,
                old->data(), old->signature());
              this->store_or_die(
                std::move(nb), true,
                std::make_unique<BlockMigration>(old_addr, bootstrap_addr));
              continue;
            }
            catch (model::MissingBlock const&)
            {}
          if (this->owner() == dn->keys().K())
          {
            if (!this->_allow_root_creation)
            {
              ELLE_WARN(
                "unable to find root block, allow creation with "
                "--allow-root-creation");
            }
            else if (root_cache && bfs::exists(*root_cache))
            {
              ELLE_WARN(
                "refusing to recreate root block, marker set: %s", root_cache);
            }
            else
            {
              ELLE_TRACE("create missing root block")
              {
                auto root = dn->make_block<ACLBlock>();
                auto address = root->address();
                this->store_or_die(move(root), true,
                                   std::make_unique<InsertRootBlock>(address));
                this->_root_address = address;
              }
              ELLE_TRACE("create missing root bootstrap block")
              {
                auto saddr = elle::sprintf("%x", this->_root_address);
                elle::Buffer baddr = elle::Buffer(saddr.data(), saddr.size());
                auto k =
                  std::make_shared<elle::cryptography::rsa::PublicKey>(
                    this->owner());
                auto nb = std::make_unique<dht::NB>(
                  *dn, k, bootstrap_name, baddr);
                auto address = nb->address();
                this->store_or_die(
                  std::move(nb),
                  true,
                  std::make_unique<InsertRootBootstrapBlock>(address));
                if (root_cache)
                  bfs::ofstream(*root_cache) << saddr;
              }
              on_root_block_create();
              return this->_root_address;
            }
          }
          elle::reactor::sleep(1_sec);
        }
      }
    }

    elle::cryptography::rsa::PublicKey const&
    FileSystem::owner() const
    {
      if (this->_owner)
        return *this->_owner;
      auto dn = std::dynamic_pointer_cast<dht::Doughnut>(this->_block_store);
      return *dn->owner();
    }

    std::pair<bool, bool>
    get_permissions(model::Model& m, model::blocks::Block const& block)
    {
      auto& dn = dynamic_cast<model::doughnut::Doughnut&>(m);
      auto acb = dynamic_cast<const model::doughnut::ACB*>(&block);
      bool r = false, w = false;
      ELLE_ASSERT(acb);
      if (dn.keys().K() == *acb->owner_key())
        return std::make_pair(true, true);
      for (auto const& e: acb->acl_entries())
      {
        if (e.read <= r && e.write <= w)
          continue; // this entry doesnt add any perm
        if (e.key == dn.keys().K())
        {
          r = r || e.read;
          w = w || e.write;
          if (r && w)
            return std::make_pair(r, w);
        }
      }
      int idx = 0;
      for (auto const& e: acb->acl_group_entries())
      {
        if (e.read <= r && e.write <= w)
        {
          ++idx;
          continue; // this entry doesnt add any perm
        }
        try
        {
          model::doughnut::Group g(dn, e.key);
          auto keys = g.group_keys();
          if (acb->group_version()[idx] < signed(keys.size()))
          {
            r = r || e.read;
            w = w || e.write;
            if (r && w)
              return std::make_pair(r, w);
          }
        }
        catch (elle::Error const& e)
        {
          ELLE_DEBUG("error accessing group: %s", e);
        }
        ++idx;
      }
      auto wp = elle::unconst(acb)->get_world_permissions();
      r = r || wp.first;
      w = w || wp.second;
      return std::make_pair(r, w);
    }

    void
    FileSystem::ensure_permissions(model::blocks::Block const& block,
                                   bool r, bool w)
    {
      auto perms = get_permissions(*_block_store, block);
      if (perms.first < r || perms.second < w)
        throw rfs::Error(EACCES, "Access denied.");
    }

    std::shared_ptr<elle::reactor::filesystem::Path>
    FileSystem::path(std::string const& path)
    {
      // cache cleanup, this place is as good as any
      if (max_cache_size >= 0)
      {
        while (_file_cache.size() > unsigned(max_cache_size))
          _file_cache.get<1>().erase(_file_cache.get<1>().begin());
        while (_directory_cache.size() > unsigned(max_cache_size))
          _directory_cache.get<1>().erase(_directory_cache.get<1>().begin());
      }
      ELLE_ASSERT(!path.empty() && path[0] == '/');
      std::vector<std::string> components;
      boost::algorithm::split(components, path, boost::algorithm::is_any_of("/\\"));
      ELLE_DEBUG("%s: get %s (%s)", this, path, components);
      ELLE_ASSERT_EQ(components.front(), "");
      bfs::path current_path("/");
      auto d = get(current_path, this->root_address());
      std::shared_ptr<DirectoryData> dp;
      for (int i=1; i< signed(components.size()) - 1; ++i)
      {
        std::string& name = components[i];
        if (name.empty() || name == ".")
          continue;
        if (name.size() > strlen("$xattrs.")
          && name.substr(0, strlen("$xattrs.")) == "$xattrs.")
        {
          auto rpath = bfs::path(
            path.substr(0, path.find("$xattrs."))) / name.substr(strlen("$xattrs."));
          auto target = this->path(rpath.string());
          std::shared_ptr<rfs::Path> xroot = std::make_shared<XAttributeDirectory>(target);
          for (int j=i+1; j < signed(components.size()) ; ++j)
            xroot = xroot->child(components[j]);
          return xroot;
        }
        ELLE_DEBUG_SCOPE("get sub-component %s", name);
        auto const& files = d->files();
        auto it = files.find(name);
        if (it == files.end() || it->second.first != EntryType::directory)
        {
          ELLE_DEBUG("%s: component '%s' is not a directory", this, name);
          THROW_NOTDIR();
        }
        dp = d;
        current_path /= name;
        d = get(current_path,
                Address(it->second.second.value(), model::flags::mutable_block, false));
      }
      std::string& name = components.back();
      if (name.empty() || name == ".")
      {
        return std::shared_ptr<rfs::Path>(new Directory(*this, d, dp, name));
      }

      static const char* attr_key = "$xattr.";
      if (name.size() > strlen(attr_key)
        && name.substr(0, strlen(attr_key)) == attr_key)
      {
        return std::make_shared<XAttributeFile>(
          this->path(bfs::path(path).parent_path().string()),
          name.substr(strlen(attr_key)));
      }
      if (name.size() > strlen("$xattrs.")
        && name.substr(0, strlen("$xattrs.")) == "$xattrs.")
      {
        auto fname = name.substr(strlen("$xattrs."));
        auto target = this->path(
          (bfs::path(path).parent_path() / fname).string());
        return std::make_shared<XAttributeDirectory>(target);
      }

      auto const& files = d->files();
      auto it = files.find(name);
      if (it == files.end())
        return std::shared_ptr<rfs::Path>(new Unknown(*this, d, name));

      auto address = Address(it->second.second.value(), model::flags::mutable_block, false);
      switch(it->second.first)
      {
      case EntryType::pending:
        return std::shared_ptr<rfs::Path>(new Unknown(*this, d, name));
      case EntryType::symlink:
        return std::shared_ptr<rfs::Path>(new Symlink(*this, address, d, name));
      case EntryType::file:
        {
          static elle::Bench bench_hit("bench.filesystem.filecache.hit", 1000_sec);
          ELLE_DEBUG("fetching %f from file cache", address);
          auto fit = _file_cache.find(address);
          boost::optional<int> version;
          if (fit != _file_cache.end())
            version = (*fit)->block_version();
          std::unique_ptr<model::blocks::Block> block;
          try
          {
            block = fetch_or_die(address, version, current_path);
            if (block)
              block->data();
          }
          catch (infinit::model::doughnut::ValidationFailed const& e)
          {
            ELLE_TRACE("perm exception %s", e);
            return std::make_shared<Unreachable>(*this, std::move(block), d,
              name, address, EntryType::file);
          }
          catch (elle::reactor::filesystem::Error const& e)
          {
            if (e.error_code() == EACCES)
            {
              return std::make_shared<Unreachable>(*this, std::move(block), d,
                name, address, EntryType::file);
            }
            else
              throw e;
          }
          fit = _file_cache.find(address);
          std::shared_ptr<FileData> fd;
          bench_hit.add(block ? 0 : 1);
          std::pair<bool, bool> perms;
          if (block)
            perms = get_permissions(*_block_store, *block);
          if (!block)
          {
            fd = *fit;
            _file_cache.modify(fit,
              [](std::shared_ptr<FileData>& d) {d->_last_used = now();});
          }
          else if (fit != _file_cache.end())
          {
            fd = *fit;
            _file_cache.modify(fit,
              [](std::shared_ptr<FileData>& d) {d->_last_used = now();});
            (*fit)->update(*block, perms, block_size().value_or(File::default_block_size));
          }
          else
          {
            fd = std::make_shared<FileData>(current_path / name, *block, perms,
              block_size().value_or(File::default_block_size));
            _file_cache.insert(fd);
          }
        return std::shared_ptr<rfs::Path>(new File(*this, address, fd, d, name));
        }
      case EntryType::directory:
        {
          try
          {
            auto dd = get(current_path / name, address);
            return std::shared_ptr<rfs::Path>(new Directory(*this, dd, d, name));
          }
          catch (elle::reactor::filesystem::Error const& e)
          {
            if (e.error_code() == EACCES)
            {
              auto fit = _file_cache.find(address);
              boost::optional<int> version;
              if (fit != _file_cache.end())
                version = (*fit)->block_version();
              auto block = fetch_or_die(address, version, current_path);
              return std::make_shared<Unreachable>(*this, std::move(block), d,
                name, address, EntryType::directory);
            }
            else
              throw e;
          }
        }
      }
      elle::unreachable();
    }

    std::shared_ptr<DirectoryData>
    FileSystem::get(bfs::path path, model::Address address)
    {
      ELLE_DEBUG_SCOPE("%s: get directory at %f", this, address);
      static elle::Bench bench_hit("bench.filesystem.dircache.hit", 1000_sec);
      boost::optional<int> version;
      auto it = _directory_cache.find(address);
      if (it != _directory_cache.end())
        version = (*it)->block_version();
      auto block = fetch_or_die(address, version, path); //invalidates 'it'
      it = _directory_cache.find(address);
      std::pair<bool, bool> perms;
      if (block)
        perms = get_permissions(*_block_store, *block);
      if (!block)
      {
        bench_hit.add(1);
        ELLE_ASSERT(it != _directory_cache.end());
        _directory_cache.modify(it,
          [](std::shared_ptr<DirectoryData>& d) {d->_last_used = now();});
        return *it;
      }
      bench_hit.add(0);
      if (it != _directory_cache.end())
      {
        _directory_cache.modify(it,
          [](std::shared_ptr<DirectoryData>& d) {d->_last_used = now();});
        (*it)->update(*block, perms);
      }
      else
      {
        auto dd = std::make_shared<DirectoryData>(path, *block, perms);
        _directory_cache.insert(dd);
        return dd;
      }
      return *it;
    }
  }
}

namespace std
{
}
