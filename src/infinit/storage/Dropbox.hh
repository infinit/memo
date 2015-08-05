#ifndef INFINIT_STORAGE_DROPBOX_HH
# define INFINIT_STORAGE_DROPBOX_HH

# include <dropbox/Dropbox.hh>

# include <infinit/storage/Key.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Dropbox
      : public Storage
    {
    public:
      Dropbox(std::string token);
      ~Dropbox();

    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k);
      virtual
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE(dropbox::Dropbox, dropbox);
    };
  }
}

#endif
