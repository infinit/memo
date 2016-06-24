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

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>
#include <reactor/exception.hh>

#include <cryptography/hash.hh>

#include <infinit/utility.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/serialization.hh>

#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

ELLE_LOG_COMPONENT("infinit.filesystem");

namespace rfs = reactor::filesystem;
namespace dht = infinit::model::doughnut;

namespace infinit
{
  namespace filesystem
  {
    FileSystem::FileSystem(
        std::string const& volume_name,
        std::shared_ptr<model::Model> model,
        boost::optional<boost::filesystem::path> root_block_cache_dir,
        boost::optional<boost::filesystem::path> mountpoint)
      : _block_store(std::move(model))
      , _single_mount(false)
      , _volume_name(volume_name)
      , _root_block_cache_dir(root_block_cache_dir)
      , _mountpoint(mountpoint)
      , _root_address(Address::null)
    {
      auto& dht = dynamic_cast<model::doughnut::Doughnut&>(
        *this->_block_store.get());
      auto passport = dht.passport();
      this->_read_only = !passport.allow_write();
      this->_network_name = passport.network();
    }

    void
    FileSystem::filesystem(reactor::filesystem::FileSystem* fs)
    {
      this->_filesystem = fs;
      fs->full_tree(false);
    }

    reactor::filesystem::FileSystem*
    FileSystem::filesystem()
    {
      return _filesystem;
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
    FileSystem::unchecked_remove(model::Address address)
    {
      filesystem::unchecked_remove(*block_store(), address);
    }

    void
    FileSystem::store_or_die(model::blocks::Block& block,
                             model::StoreMode mode,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      block.seal();
      auto copy = block.clone();
      store_or_die(std::move(copy), mode, std::move(resolver));
    }

    void
    FileSystem::store_or_die(std::unique_ptr<model::blocks::Block> block,
                             model::StoreMode mode,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store or die: %s", this, block);
      auto address = block->address();
      try
      {
        this->_block_store->store(std::move(block), mode, std::move(resolver));
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch (infinit::storage::InsufficientSpace const& e)
      {

        ELLE_TRACE("store_or_die: %s", e.what());
        THROW_ENOSPC;
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
                 Node* node)
    {
      try
      {
        return model.fetch(address, std::move(local_version));
      }
      catch(reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("data not found fetching \"/%s\": %s",
                  "", mb);
        throw rfs::Error(EIO, elle::sprintf("%s", mb));
      }
      catch (elle::serialization::Error const& se)
      {
        ELLE_WARN("serialization error fetching %f: %s", address, se);
        throw rfs::Error(EIO, elle::sprintf("%s", se));
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected exception fetching %f: %s", address, e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected exception on fetching %f: %s", address, e.what());
        throw rfs::Error(EIO, e.what());
      }
    }
    std::unique_ptr<model::blocks::Block>
    FileSystem::fetch_or_die(model::Address address,
                             boost::optional<int> local_version,
                             Node* node)
    {
      return filesystem::fetch_or_die(*this->block_store(), address, local_version, node);
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

    std::unique_ptr<MutableBlock>
    FileSystem::_root_block()
    {
      boost::optional<boost::filesystem::path> root_cache;
      if (this->root_block_cache_dir())
      {
        auto dir = this->root_block_cache_dir().get();
        if (!boost::filesystem::exists(dir))
          boost::filesystem::create_directories(dir);
        root_cache = dir / "root_block";
        ELLE_DEBUG("root block cache: %s", root_cache);
      }
      bool migrate = true;
      auto dn = std::dynamic_pointer_cast<dht::Doughnut>(this->_block_store);
      auto const bootstrap_name = this->_volume_name + ".root";
      Address addr = dht::NB::address(
        *dn->owner(), bootstrap_name, dn->version());
      while (true)
      {
        try
        {
          ELLE_DEBUG_SCOPE("fetch root bootstrap block at %f", addr);
          auto block = this->_block_store->fetch(addr);
          addr = Address(
            Address::from_string(block->data().string().substr(2)).value(),
            model::flags::mutable_block,
            false);
          ELLE_DEBUG_SCOPE("fetch root block at %f", addr);
          break;
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
                "migrate old bootstrap block from %s to %s", old_addr, addr);
              auto nb = elle::make_unique<dht::NB>(
                dn.get(), dn->owner(), bootstrap_name,
                old->data(), old->signature());
              this->store_or_die(std::move(nb), model::STORE_INSERT,
                                 model::make_drop_conflict_resolver());
              continue;
            }
            catch (model::MissingBlock const&)
            {}
          if (*dn->owner() == dn->keys().K())
          {
            if (root_cache && boost::filesystem::exists(*root_cache))
            {
              ELLE_WARN(
                "refusing to recreate root block, marker set: %s", root_cache);
            }
            else
            {
              std::unique_ptr<MutableBlock> mb;
              ELLE_TRACE("create missing root block")
              {
                mb = dn->make_block<ACLBlock>();
                this->store_or_die(mb->clone(), model::STORE_INSERT,
                  model::make_drop_conflict_resolver());
              }
              ELLE_TRACE("create missing root bootstrap block")
              {
                auto saddr = elle::sprintf("%x", mb->address());
                elle::Buffer baddr = elle::Buffer(saddr.data(), saddr.size());
                auto nb = elle::make_unique<dht::NB>(
                  dn.get(), dn->owner(), bootstrap_name, baddr);
                this->store_or_die(std::move(nb), model::STORE_INSERT,
                  model::make_drop_conflict_resolver());
                if (root_cache)
                  boost::filesystem::ofstream(*root_cache) << saddr;
              }
              _root_address = mb->address();
              on_root_block_create();
              return mb;
            }
          }
          reactor::sleep(1_sec);
        }
      }
      if (root_cache && !boost::filesystem::exists(*root_cache))
      {
        boost::filesystem::ofstream ofs(*root_cache);
        elle::fprintf(ofs, "%x", addr);
      }
      _root_address = addr;
      return elle::cast<MutableBlock>::runtime(fetch_or_die(addr));
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

    std::shared_ptr<reactor::filesystem::Path>
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

      if (_root_address == Address::null)
        _root_block();
      ELLE_ASSERT(!path.empty() && path[0] == '/');
      std::vector<std::string> components;
      boost::algorithm::split(components, path, boost::algorithm::is_any_of("/\\"));
      ELLE_DEBUG("%s: get %s (%s)", this, path, components);
      ELLE_ASSERT_EQ(components.front(), "");
      boost::filesystem::path current_path("/");
      auto d = get(current_path, _root_address);
      std::shared_ptr<DirectoryData> dp;
      for (int i=1; i< signed(components.size()) - 1; ++i)
      {
        std::string& name = components[i];
        if (name.empty() || name == ".")
          continue;
        if (name.size() > strlen("$xattrs.")
          && name.substr(0, strlen("$xattrs.")) == "$xattrs.")
        {
          auto rpath = boost::filesystem::path(
            path.substr(0, path.find("$xattrs."))) / name.substr(strlen("$xattrs."));
          auto target = this->path(rpath.string());
          std::shared_ptr<rfs::Path> xroot = std::make_shared<XAttributeDirectory>(target);
          for (int j=i+1; j < signed(components.size()) ; ++j)
            xroot = xroot->child(components[j]);
          return xroot;
        }

        auto const& files = d->files();
        auto it = files.find(name);
        if (it == files.end() || it->second.first != EntryType::directory)
        {
          ELLE_DEBUG("%s: component '%s' is not a directory", this, name);
          THROW_NOTDIR;
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
          this->path(boost::filesystem::path(path).parent_path().string()),
          name.substr(strlen(attr_key)));
      }
      if (name.size() > strlen("$xattrs.")
        && name.substr(0, strlen("$xattrs.")) == "$xattrs.")
      {
        auto fname = name.substr(strlen("$xattrs."));
        auto target = this->path(
          (boost::filesystem::path(path).parent_path() / fname).string());
        return std::make_shared<XAttributeDirectory>(target);
      }

      auto const& files = d->files();
      auto it = files.find(name);
      if (it == files.end())
        return std::shared_ptr<rfs::Path>(new Unknown(*this, d, name));

      auto address = Address(it->second.second.value(), model::flags::mutable_block, false);
      switch(it->second.first)
      {
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
            block = fetch_or_die(address, version);
            if (block)
              block->data();
          }
          catch (infinit::model::doughnut::ValidationFailed const& e)
          {
            ELLE_TRACE("perm exception %s", e);
            return std::make_shared<Unreachable>(*this, d, name,
              address, EntryType::file);
          }
          catch (reactor::filesystem::Error const& e)
          {
            if (e.error_code() == EACCES)
            {
              return std::make_shared<Unreachable>(*this, d, name,
                address, EntryType::file);
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
            (*fit)->update(*block, perms);
          }
          else
          {
            fd = std::make_shared<FileData>(current_path / name, *block, perms);
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
          catch (reactor::filesystem::Error const& e)
          {
            if (e.error_code() == EACCES)
            {
              return std::make_shared<Unreachable>(*this, d, name,
                address, EntryType::directory);
            }
            else
              throw e;
          }
        }
      }
      elle::unreachable();
    }

    std::shared_ptr<DirectoryData>
    FileSystem::get(boost::filesystem::path path, model::Address address)
    {
      ELLE_DEBUG_SCOPE("%s: getting directory at %s", this, address);
      static elle::Bench bench_hit("bench.filesystem.dircache.hit", 1000_sec);
      boost::optional<int> version;
      auto it = _directory_cache.find(address);
      if (it != _directory_cache.end())
        version = (*it)->block_version();
      auto block = fetch_or_die(address, version); //invalidates 'it'
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
