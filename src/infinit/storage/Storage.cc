#include <infinit/storage/Storage.hh>

#include <elle/log.hh>
#include <elle/factory.hh>

#include <infinit/storage/Key.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

ELLE_LOG_COMPONENT("infinit.storage.Storage");

namespace infinit
{
  namespace storage
  {
    Storage::~Storage()
    {}

    elle::Buffer
    Storage::get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", *this, key);
      return this->_get(key);
    }

    void
    Storage::set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_ASSERT(insert || update);
      ELLE_TRACE_SCOPE("%s: %s at %x", *this,
                       insert ? update ? "upsert" : "insert" : "update", key);
      return this->_set(key, value, insert, update);
    }

    void
    Storage::erase(Key key)
    {
      ELLE_TRACE_SCOPE("%s: erase %x", *this, key);
      return this->_erase(key);
    }

    std::vector<Key>
    Storage::list()
    {
      ELLE_TRACE_SCOPE("%s: list", *this);
      return this->_list();
    }

    BlockStatus
    Storage::status(Key k)
    {
      ELLE_TRACE_SCOPE("%s: status %x", *this, k);
      return this->_status(k);
    }

    BlockStatus
    Storage::_status(Key k)
    {
      return BlockStatus::unknown;
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Storage::print(std::ostream& out) const
    {
      elle::fprintf(out, "%s(%x)", elle::type_info(*this), this);
    }

    std::unique_ptr<Storage>
    instantiate(std::string const& name,
                std::string const& args)
    {
      ELLE_TRACE_SCOPE("Processing backend %s '%s'", args[0], args[1]);
      std::vector<std::string> bargs;
      size_t space = args.find(" ");
      const char* sep = (space == args.npos) ? ":" : " ";
      boost::algorithm::split(bargs, args, boost::algorithm::is_any_of(sep),
                              boost::algorithm::token_compress_on);
      std::unique_ptr<Storage> backend = elle::Factory<Storage>::instantiate(name, bargs);
      return std::move(backend);
    }

    /*---------------.
    | Storage Config |
    `---------------*/

    StorageConfig::StorageConfig(std::string name_)
      : name(std::move(name_))
    {}

    StorageConfig::StorageConfig(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    StorageConfig::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
    }
  }
}
