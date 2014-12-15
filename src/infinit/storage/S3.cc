#include <infinit/storage/S3.hh>

#include <elle/log.hh>


#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.s3.S3");

namespace infinit
{
  namespace storage
  {

    S3::S3(std::unique_ptr<aws::S3> storage)
    : _storage(std::move(storage))
    {}
    
    elle::Buffer
    S3::_get(Key key) const
    {
      return _storage->get_object(elle::sprintf("%x", key));
    }
    void
    S3::_set(Key key, elle::Buffer value, bool insert, bool update)
    {
      _storage->put_object(value, elle::sprintf("%x", key));
    }
    void
    S3::_erase(Key key)
    {
      _storage->delete_object(elle::sprintf("%x", key));
    }
  }
}