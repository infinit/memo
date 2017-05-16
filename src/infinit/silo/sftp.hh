#pragma once

#include <elle/reactor/asio.hh>

#include <elle/reactor/semaphore.hh>

#include <infinit/silo/Silo.hh>

namespace infinit
{
  namespace silo
  {
    class SFTP: public Storage
    {
    public:
      SFTP(std::string const& host, std::string const& path);
      std::string
      type() const override { return "sftp"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
    private:
      void _connect();
      int _fin;
      int _fout;
      mutable boost::asio::posix::stream_descriptor _in, _out;
      std::string _server_address;
      std::string _path;
      int _child;
      mutable elle::reactor::Semaphore _sem;
      mutable int _req;
    };

    struct SFTPStorageConfig
      : public StorageConfig
    {
      SFTPStorageConfig(std::string const& name,
                        std::string const& host,
                        std::string const& path,
                        boost::optional<int64_t> capacity,
                        boost::optional<std::string> description);
      SFTPStorageConfig(elle::serialization::SerializerIn& in);

      void
      serialize(elle::serialization::Serializer& s) override;

      std::unique_ptr<infinit::silo::Storage>
      make() override;

      std::string host;
      std::string path;
    };
  }
}
