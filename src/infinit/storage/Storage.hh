#ifndef INFINIT_STORAGE_HH
# define INFINIT_STORAGE_HH

# include <iosfwd>
# include <cstdint>

# include <elle/Buffer.hh>
# include <elle/attribute.hh>

# include <infinit/storage/fwd.hh>

namespace infinit
{
  namespace storage
  {
    class Storage
    {
    public:
      elle::Buffer
      get(Key k) const;
      void
      set(Key k, elle::Buffer const& value,
          bool insert = true, bool update = false);
      void
      erase(Key k);
    protected:
      virtual
      elle::Buffer
      _get(Key k) const = 0;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) = 0;
      virtual
      void
      _erase(Key k) = 0;
    };
  }
}

#endif
