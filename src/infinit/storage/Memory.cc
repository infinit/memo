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
      else
      {
        auto& buffer = it->second;
        return elle::Buffer(buffer.contents(), buffer.size());
      }
    }

    std::size_t
    Memory::size() const
    {
      std::size_t res = 0;
      for (auto const& block: *this->_blocks)
        res += block.second.size();
      return res;
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
        auto i = this->_blocks->find(key);
        if (i == this->_blocks->end())
          throw MissingKey(key);
        else
        {
          ELLE_DEBUG("%s: block updated", *this);
          i->second = elle::Buffer(value.contents(), value.size());
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
      else
        // FIXME: impl.
        return 0;
    }

    std::vector<Key>
    Memory::_list()
    {
      return elle::make_vector(*this->_blocks,
                               [](auto const& b)
                               {
                                 return b.first;
                               });
    }

    void
    MemoryStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
    }

    std::unique_ptr<infinit::storage::Storage>
    MemoryStorageConfig::make()
    {
      return std::make_unique<infinit::storage::Memory>();
    }
  }
}

namespace
{
  const auto reg
    = elle::serialization::Hierarchy<infinit::storage::StorageConfig>::
    Register<infinit::storage::MemoryStorageConfig>("memory");

  std::unique_ptr<infinit::storage::Storage>
  make(std::vector<std::string> const& args)
  {
    return std::make_unique<infinit::storage::Memory>();
  }

  FACTORY_REGISTER(infinit::storage::Storage, "memory", make);
}
