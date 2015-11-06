#ifndef INFINIT_STORAGE_HH
# define INFINIT_STORAGE_HH

# include <iosfwd>
# include <cstdint>

# include <elle/Buffer.hh>
# include <elle/attribute.hh>
# include <elle/serialization/Serializer.hh>

# include <infinit/model/Address.hh>
# include <infinit/serialization.hh>
# include <infinit/storage/fwd.hh>

namespace infinit
{
  namespace storage
  {
    enum class BlockStatus
    {
      exists,
      missing,
      unknown
    };

    class Storage
      : public elle::Printable
    {
    public:
      virtual
      ~Storage();
      elle::Buffer
      get(Key k) const;
      void
      set(Key k, elle::Buffer const& value,
          bool insert = true, bool update = false);
      void
      erase(Key k);
      std::vector<Key>
      list();
      BlockStatus
      status(Key k);
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
      virtual
      std::vector<Key>
      _list() = 0;
      /// Return the status of a given key.
      /// Implementations should check localy only if the information is
      /// available, or return BlockStatus::unknown.
      virtual
      BlockStatus
      _status(Key k);

    /*----------.
    | Printable |
    `----------*/
    public:
      virtual
      void
      print(std::ostream& out) const override;
    };

    std::unique_ptr<Storage>
    instantiate(std::string const& name,
                std::string const& args);

    struct StorageConfig
      : public elle::serialization::VirtuallySerializable
    {
      StorageConfig() = default;
      StorageConfig(std::string name);
      StorageConfig(elle::serialization::SerializerIn& input);
      virtual
      void
      serialize(elle::serialization::Serializer& s);
      typedef infinit::serialization_tag serialization_tag;
      static constexpr char const* virtually_serializable_key = "type";
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() = 0;

      std::string name;
    };
  }
}


#endif
