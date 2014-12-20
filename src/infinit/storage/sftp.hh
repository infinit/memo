#ifndef INFINIT_STORAGE_SFTP_HH
#define INFINIT_STORAGE_SFTP_HH

#include <infinit/storage/Storage.hh>
namespace infinit
{
  namespace storage
  {
    class SFTP: public Storage
    {
    public:
      SFTP(std::string const& host, std::string const& path);
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
    private:
      void _connect();
      int _in;
      int _out;
      std::string _server_address;
      std::string _path;
      int _child;
      mutable int _req;
    };
  }
}

#endif