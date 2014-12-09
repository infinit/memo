#ifndef INFINIT_STORAGE_FILESYSTEM_HH
# define INFINIT_STORAGE_FILESYSTEM_HH

# include <boost/filesystem/path.hpp>

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Filesystem
      : public Storage
    {
    public:
      Filesystem(boost::filesystem::path root);
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k);
      ELLE_ATTRIBUTE_R(boost::filesystem::path, root);
    private:
      boost::filesystem::path
      _path(Key const& key) const;
    };
  }
}

#endif
