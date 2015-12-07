#include <infinit/filesystem/filesystem.hh>
#include <infinit/filesystem/AnyBlock.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>

#include <infinit/model/MissingBlock.hh>

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

#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/serialization.hh>


#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

ELLE_LOG_COMPONENT("infinit.filesystem");

namespace rfs = reactor::filesystem;

namespace infinit
{
  namespace filesystem
  {
    FileSystem::FileSystem(std::string const& volume_name,
                           std::shared_ptr<model::Model> model)
      : _block_store(std::move(model))
      , _single_mount(false)
      , _volume_name(volume_name)
    {
#ifndef INFINIT_WINDOWS
      reactor::scheduler().signal_handle
        (SIGUSR1, [this] { this->print_cache_stats();});
#endif
    }

    void
    FileSystem::unchecked_remove(model::Address address)
    {
      try
      {
        _block_store->remove(address);
      }
      catch (model::MissingBlock const&)
      {
        ELLE_DEBUG("%s: block was not published", *this);
      }
      catch (elle::Exception const& e)
      {
        ELLE_ERR("%s: unexpected exception: %s", *this, e.what());
        throw;
      }
      catch (...)
      {
        ELLE_ERR("%s: unknown exception", *this);
        throw;
      }
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
      ELLE_TRACE_SCOPE("%s: store or die: %s", *this, *block);
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
                  block->address(), e);
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::Block>
    FileSystem::fetch_or_die(model::Address address,
                             boost::optional<int> local_version,
                             Node* node)
    {
      try
      {
        return this->_block_store->fetch(address, std::move(local_version));
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
                  node ? node->full_path().string() : "", mb);
        if (node)
          node->_remove_from_cache();
        throw rfs::Error(EIO, elle::sprintf("%s", mb));
      }
      catch (elle::serialization::Error const& se)
      {
        ELLE_WARN("serialization error fetching %x: %s", address, se);
        throw rfs::Error(EIO, elle::sprintf("%s", se));
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected exception fetching %x: %s", address, e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected exception on fetching %x: %s", address, e.what());
        throw rfs::Error(EIO, e.what());
      }
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

    void
    FileSystem::print_cache_stats()
    {
      auto root = std::dynamic_pointer_cast<Directory>(filesystem()->path("/"));
      CacheStats stats;
      memset(&stats, 0, sizeof(CacheStats));
      root->cache_stats(stats);
      std::cerr << "Statistics:\n"
      << stats.directories << " dirs\n"
      << stats.files << " files\n"
      << stats.blocks <<" blocks\n"
      << stats.size << " bytes"
      << std::endl;
    }

    std::shared_ptr<rfs::Path>
    FileSystem::path(std::string const& path)
    {
      ELLE_TRACE_SCOPE("%s: fetch root", *this);
      // In the infinit filesystem, we never query a path other than the root.
      ELLE_ASSERT_EQ(path, "/");
      auto root = this->_root_block();
      ELLE_ASSERT(!!root);
      auto acl_root =  elle::cast<ACLBlock>::runtime(std::move(root));
      ELLE_ASSERT(!!acl_root);
      auto res =
        std::make_shared<Directory>(nullptr, *this, "", acl_root->address());
      res->_block = std::move(acl_root);
      return res;
    }

    std::unique_ptr<MutableBlock>
    FileSystem::_root_block()
    {
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(_block_store);
      Address addr =
        model::doughnut::NB::address(dn->owner(), _volume_name + ".root");
      while (true)
      {
        try
        {
          ELLE_DEBUG_SCOPE("fetch root boostrap block at %x", addr);
          auto block = _block_store->fetch(addr);
          addr = Address::from_string(block->data().string().substr(2));
          break;
        }
        catch (model::MissingBlock const& e)
        {
          if (dn->owner() == dn->keys().K())
          {
            std::unique_ptr<MutableBlock> mb = dn->make_block<ACLBlock>();
            auto saddr = elle::sprintf("%x", mb->address());
            elle::Buffer baddr = elle::Buffer(saddr.data(), saddr.size());
            ELLE_TRACE("create missing root block")
            {
              auto cpy = mb->clone();
              this->store_or_die(std::move(cpy), model::STORE_INSERT);
            }
            ELLE_TRACE("create missing root bootstrap block")
            {
              auto nb = elle::make_unique<model::doughnut::NB>(
                dn.get(), dn->owner(), this->_volume_name + ".root", baddr);
              this->store_or_die(std::move(nb), model::STORE_INSERT);
            }
            return mb;
          }
          reactor::sleep(1_sec);
        }
      }
      return elle::cast<MutableBlock>::runtime(fetch_or_die(addr));
    }

    std::pair<bool, bool>
    FileSystem::get_permissions(model::blocks::Block const& block)
    {
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(block_store());
      auto keys = dn->keys();
      auto acl = elle::unconst(dynamic_cast<const model::blocks::ACLBlock*>(&block));
      ELLE_ASSERT(acl);
      auto res = umbrella([&] {
        for (auto const& e: acl->list_permissions(*dn))
        {
          auto u = dynamic_cast<model::doughnut::User*>(e.user.get());
          if (!u)
            continue;
          auto hit = u->key() == keys.K();
          if (hit)
            return std::make_pair(e.read, e.write);
        }
        return acl->get_world_permissions();
        //throw rfs::Error(EACCES, "Access denied.");
      });
      return res;
    }

    void
    FileSystem::ensure_permissions(model::blocks::Block const& block,
                                   bool r, bool w)
    {
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(block_store());
      auto keys = dn->keys();
      auto acl = elle::unconst(dynamic_cast<const model::blocks::ACLBlock*>(&block));
      ELLE_ASSERT(acl);
      umbrella([&] {
        for (auto const& e: acl->list_permissions(*dn))
        {
          auto u = dynamic_cast<model::doughnut::User*>(e.user.get());
          if (!u)
            continue;
          if (e.write >= w && e.read >= r && u->key() == keys.K())
            return;
        }
        auto wp = acl->get_world_permissions();
        if (wp.first < r || wp.second < w)
          throw rfs::Error(EACCES, "Access denied.");
      });
    }
  }
}
