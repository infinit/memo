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

    auto
    Memory::_find(Key key)
      -> typename Blocks::iterator
    {
      auto it = this->_blocks->find(key);
      if (it == this->_blocks->end())
        throw MissingKey(key);
      else
        return it;
    }

    auto
    Memory::_find(Key key) const
      -> typename Blocks::const_iterator
    {
      auto it = this->_blocks->find(key);
      if (it == this->_blocks->end())
        throw MissingKey(key);
      else
        return it;
    }

    elle::Buffer
    Memory::_get(Key key) const
    {
      return _find(key)->second;
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
        auto p = this->_blocks->emplace(key, elle::Buffer());
        if (!p.second && !update)
          throw Collision(key);
        p.first->second = elle::Buffer(value.contents(), value.size());
        if (p.second)
          ELLE_DEBUG("%s: block inserted", *this);
        else if (!p.second)
          ELLE_DEBUG("%s: block updated: %s", *this,
                     this->get(key));
      }
      else
      {
        _find(key)->second = elle::Buffer(value.contents(), value.size());
        ELLE_DEBUG("%s: block updated", *this);
      }
      // FIXME: impl.
      return 0;
    }

    int
    Memory::_erase(Key key)
    {
      auto it = _find(key);
      this->_blocks->erase(it);
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
