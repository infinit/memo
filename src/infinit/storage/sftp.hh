#ifndef INFINIT_STORAGE_SFTP_HH
#define INFINIT_STORAGE_SFTP_HH

#include <boost/asio.hpp>

#include <infinit/storage/Storage.hh>

#include <reactor/semaphore.hh>
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
      virtual
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
      mutable reactor::Semaphore _sem;
      mutable int _req;
    };
  }
}

#endif