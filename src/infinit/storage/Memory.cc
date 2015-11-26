#include <infinit/storage/Memory.hh>

#include <elle/factory.hh>
#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Memory");

namespace infinit
{
  namespace storage
  {
    Memory::Memory()
      : _blocks(new Blocks, std::default_delete<Blocks>())
    {}

    Memory::Memory(Blocks& blocks)
      : _blocks(&blocks, [] (Blocks*) {})
    {}

    elle::Buffer
    Memory::_get(Key key) const
    {
      auto it = this->_blocks->find(key);
      if (it == this->_blocks->end())
        throw MissingKey(key);
      auto& buffer = it->second;
      return elle::Buffer(buffer.contents(), buffer.size());
    }

    int
    Memory::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      if (insert)
      {
        auto insertion =
          this->_blocks->insert(std::make_pair(key, elle::Buffer()));
        if (!insertion.second && !update)
          throw Collision(key);
        insertion.first->second = elle::Buffer(value.contents(), value.size());
        if (insertion.second)
          ELLE_DEBUG("%s: block inserted", *this);
        else if (!insertion.second)
          ELLE_DEBUG("%s: block updated: %s", *this,
                     this->get(key));
      }
      else
      {
        auto search = this->_blocks->find(key);
        if (search == this->_blocks->end())
          throw MissingKey(key);
        else
        {
          ELLE_DEBUG("%s: block updated", *this);
          search->second = elle::Buffer(value.contents(), value.size());
        }
      }
      // FIXME: impl.
      return 0;
    }

    int
    Memory::_erase(Key key)
    {
      if (this->_blocks->erase(key) == 0)
        throw MissingKey(key);

      // FIXME: impl.
      return 0;
    }

    std::vector<Key>
    Memory::_list()
    {
      std::vector<Key> res;
      for (auto const& b: *this->_blocks)
        res.push_back(b.first);
      return res;
    }

    static std::unique_ptr<Storage> make(std::vector<std::string> const& args)
    {
      return elle::make_unique<infinit::storage::Memory>();
    }

    MemoryStorageConfig::MemoryStorageConfig(std::string name, int64_t capacity)
      : StorageConfig(std::move(name), std::move(capacity))
    {}

    MemoryStorageConfig::MemoryStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
    {
      this->serialize(input);
    }

    void
    MemoryStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
    }

    std::unique_ptr<infinit::storage::Storage>
    MemoryStorageConfig::make()
    {
      return elle::make_unique<infinit::storage::Memory>();
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<MemoryStorageConfig> _register_MemoryStorageConfig("memory");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "memory", &infinit::storage::make);
