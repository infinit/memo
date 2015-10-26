#ifndef INFINIT_STORAGE_S3_HH
# define INFINIT_STORAGE_S3_HH

# include <infinit/storage/Storage.hh>
# include <aws/S3.hh>

namespace infinit
{
  namespace storage
  {
    class S3
      : public Storage
    {
    public:
      S3(std::unique_ptr<aws::S3>);
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE_RX(std::unique_ptr<aws::S3>, storage);
    };
  }
}

#endif