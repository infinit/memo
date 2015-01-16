#ifndef INFINIT_STORAGE_HH
# define INFINIT_STORAGE_HH

# include <iosfwd>
# include <cstdint>

# include <elle/Buffer.hh>
# include <elle/attribute.hh>
# include <elle/serialization/Serializer.hh>

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

    std::unique_ptr<Storage>
    instantiate(std::string const& name,
                std::string const& args);
    struct StorageConfig:
    public elle::serialization::VirtuallySerializable
    {
      static constexpr char const* virtually_serializable_key = "type";
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() const = 0;
    };
  }
}


#endif
