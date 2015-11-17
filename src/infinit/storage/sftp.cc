#include <unistd.h>
#ifndef INFINIT_WINDOWS
#include <arpa/inet.h>
#include <sys/wait.h>
#endif

#include <boost/asio.hpp>

#include <reactor/scheduler.hh>
#include <reactor/Barrier.hh>
#include <reactor/lockable.hh>

#include <infinit/storage/sftp.hh>
#include <infinit/storage/MissingKey.hh>
#include <infinit/model/Address.hh>

#include <elle/log.hh>
#include <elle/factory.hh>

enum PacketType {
  SSH_FXP_INIT          =      1 ,
  SSH_FXP_VERSION       =      2 ,
  SSH_FXP_OPEN          =      3 ,
  SSH_FXP_CLOSE         =      4 ,
  SSH_FXP_READ          =      5 ,
  SSH_FXP_WRITE         =      6 ,
  SSH_FXP_LSTAT         =      7 ,
  SSH_FXP_FSTAT         =      8 ,
  SSH_FXP_SETSTAT       =      9 ,
  SSH_FXP_FSETSTAT      =     10 ,
  SSH_FXP_OPENDIR       =     11 ,
  SSH_FXP_READDIR       =     12 ,
  SSH_FXP_REMOVE        =     13 ,
  SSH_FXP_MKDIR         =     14 ,
  SSH_FXP_RMDIR         =     15 ,
  SSH_FXP_REALPATH      =     16 ,
  SSH_FXP_STAT          =     17 ,
  SSH_FXP_RENAME        =     18 ,
  SSH_FXP_READLINK      =     19 ,
  SSH_FXP_SYMLINK       =     20 ,
  SSH_FXP_STATUS        =    101 ,
  SSH_FXP_HANDLE        =    102 ,
  SSH_FXP_DATA          =    103 ,
  SSH_FXP_NAME          =    104 ,
  SSH_FXP_ATTRS         =    105 ,
  SSH_FXP_EXTENDED      =    200 ,
  SSH_FXP_EXTENDED_REPLY=    201 ,
};

#define SSH_FXF_READ            0x00000001
#define SSH_FXF_WRITE           0x00000002
#define SSH_FXF_APPEND          0x00000004
#define SSH_FXF_CREAT           0x00000008
#define SSH_FXF_TRUNC           0x00000010
#define SSH_FXF_EXCL            0x00000020

#define SSH_FX_OK                            0
#define SSH_FX_EOF                           1
#define SSH_FX_NO_SUCH_FILE                  2
#define SSH_FX_PERMISSION_DENIED             3
#define SSH_FX_FAILURE                       4
#define SSH_FX_BAD_MESSAGE                   5
#define SSH_FX_NO_CONNECTION                 6
#define SSH_FX_CONNECTION_LOST               7
#define SSH_FX_OP_UNSUPPORTED                8


#define SSH_FILEXFER_ATTR_SIZE          0x00000001
#define SSH_FILEXFER_ATTR_UIDGID        0x00000002
#define SSH_FILEXFER_ATTR_PERMISSIONS   0x00000004
#define SSH_FILEXFER_ATTR_ACMODTIME     0x00000008
#define SSH_FILEXFER_ATTR_EXTENDED      0x80000000

ELLE_LOG_COMPONENT("infinit.fs.sftp");
static std::unique_ptr<infinit::storage::Storage> make(std::vector<std::string> const& args)
{
  return elle::make_unique<infinit::storage::SFTP>(args[0], args[1]);
}

FACTORY_REGISTER(infinit::storage::Storage, "sftp", &make);
namespace infinit
{
  namespace storage
  {
    class PacketError: public std::runtime_error
    {
    public:
      PacketError(int erc, std::string const& what)
      : std::runtime_error(what.c_str())
      , _erc(erc)
      {}
    private:
      ELLE_ATTRIBUTE_R(int, erc);
    };
    class Packet: public elle::Buffer
    {
    public:
      void add(PacketType v);
      void add(int v);
      void add(std::string const & v);
      void add(elle::ConstWeakBuffer const& v);
      void addSize();
      template<typename T> void _make(T const& v)
      {
        add(v);
        addSize();
      }
      template<typename H, typename ...T>
      void _make(H const& h, T const& ...v)
      {
        add(h);
        _make(v...);
      }
      template<typename H, typename ...T>
      void make(H const& h, T const& ...v)
      {
        size(0);
        _payload = elle::ConstWeakBuffer();
        add(0); // size placeholder
        add(h);
        _make(v...);
      }

      void readFrom(boost::asio::posix::stream_descriptor& s);
      void writeTo(boost::asio::posix::stream_descriptor& s);

      unsigned char readByte();
      int readInt();
      elle::ConstWeakBuffer readString();
      void expectType(int t); // eats the type

      void resetRead();
      void skipAttr();
    private:
      unsigned int _pos;
      elle::ConstWeakBuffer _payload;
    };
    void Packet::add(PacketType v)
    {
      char pt = (char)v;
      append(&pt, 1);
    }
    void Packet::add(int v)
    {
      v = htonl(v);
      append(&v, 4);
    }
    void Packet::add(std::string const& v)
    {
      add(v.length());
      append(v.c_str(), v.size());
    }
    void Packet::add(elle::ConstWeakBuffer const& v)
    {
      ELLE_DEBUG("Add buffer of size %s", v.size());
      add(v.size());
      _payload = v;
    }
    void Packet::addSize()
    {
      int sz = size() - 4 + _payload.size();
      sz = htonl(sz);
      *(int*)mutable_contents() = sz;
    }
    void Packet::readFrom(boost::asio::posix::stream_descriptor& s)
    {
      ELLE_DEBUG("Reading one packet...");
      _pos = 0;
      namespace asio = boost::asio;
      size(4);
      reactor::Barrier b;
      boost::system::error_code erc;
      size_t len = 0;
      auto cb2 = [&](boost::system::error_code e, size_t sz)
      {
        ELLE_DEBUG("got payload");
        if (e)
          ELLE_ERR("socket error: %s", e.message());
        else if (sz != len)
          ELLE_ERR("Unexpected read %s != %s", sz, len);
        erc = e;
        b.open();
      };
      s.non_blocking(false);
      size_t sz = asio::read(s, asio::buffer(mutable_contents(), 4),
        asio::transfer_exactly(4), erc);

      if (!erc)
        ELLE_ASSERT_EQ(sz, 4u);
      if (erc)
        throw std::runtime_error(erc.message());
      len = this->readInt();
      ELLE_DEBUG("got header, reading %s", len);
      this->size(4+len);
      if (len < 1024)
      {
        sz = asio::read(s, asio::buffer(mutable_contents()+4, len),
          asio::transfer_exactly(len), erc);
        ELLE_DEBUG("got payload");
        if (erc)
          ELLE_ERR("socket error: %s", erc.message());
        else if (sz != len)
          ELLE_ERR("Unexpected read %s != %s", sz, len);
      }
      else
      {
        s.non_blocking(true);
        asio::async_read(s, asio::buffer(mutable_contents()+4, len),
          asio::transfer_exactly(len), cb2);
        b.wait();
      }

      /*
      asio::async_read(s, asio::buffer(mutable_contents(), 4),
        asio::transfer_exactly(4),
        [&](boost::system::error_code e, size_t sz)
          {
            if (!e)
              ELLE_ASSERT_EQ(sz, 4);
            if (e)
            {
              erc = e;
              b.open();
              return;
            }
            len = this->readInt();
            ELLE_DEBUG("got header, reading %s", len);
            this->size(4+len);
            asio::async_read(s, asio::buffer(mutable_contents()+4, len),
              asio::transfer_exactly(len), cb2);
          });*/

      ELLE_DEBUG("Reading done");
      if (erc)
        throw std::runtime_error(erc.message());
    }
    void Packet::writeTo(boost::asio::posix::stream_descriptor& s)
    {
      ELLE_DEBUG("Writing packet %x ..., with payload %s", *this, _payload.size());
      namespace asio = boost::asio;
      std::vector<asio::const_buffer> v;
      v.push_back(asio::const_buffer(contents(), size()));
      if (_payload.size())
        v.push_back(asio::const_buffer(_payload.contents(), _payload.size()));
      boost::system::error_code erc;

      bool mark = false;
      if (_payload.size() < 1024)
      {
        unsigned sz = asio::write(s, v, erc);
        if (!erc)
          ELLE_ASSERT_EQ(sz, size() + _payload.size());
      }
      else
      {
        reactor::Semaphore b;
        asio::async_write(s, v, [&](boost::system::error_code e, size_t sz)
          {
            if (e)
              ELLE_ERR("socket error: %s", e.message());
            else if (sz != size() + _payload.size())
              ELLE_ERR("Unexpected read %s != %s", sz, size() + _payload.size());
            erc = e;
            mark = true;
            ELLE_DEBUG("write finished, opening...");
            b.release();
          });
        b.wait();
        ELLE_ASSERT(mark);
      }
      ELLE_DEBUG("...write done");
      if (erc)
        throw std::runtime_error(erc.message());
    }
    unsigned char Packet::readByte()
    {
      unsigned char res = contents()[_pos];
      ++_pos;
      return res;
    }
    int Packet::readInt()
    {
      int v = *(int*)(contents()+_pos);
      v = ntohl(v);
      _pos += 4;
      return v;
    }
    elle::ConstWeakBuffer Packet::readString()
    {
      int len = readInt();
      auto res = elle::ConstWeakBuffer(contents() + _pos, len);
      _pos += len;
      return res;
    }
    void Packet::expectType(int t)
    {
      int type = readByte();
      if (type == SSH_FXP_STATUS)
      { // error
        readInt(); // request id
        int erc = readInt();
        std::string erm = readString().string();
        throw PacketError(erc,
          elle::sprintf("request failed with %s: %s", erc, erm));
      }
      else if (type != t)
      {
        throw PacketError(0,
          elle::sprintf("Expected type HANDLE or STATUS, got %s", type));
      }
    }

    void Packet::skipAttr()
    {
      int flags = readInt();
      if (flags & SSH_FILEXFER_ATTR_SIZE) { readInt(); readInt(); }
      if (flags & SSH_FILEXFER_ATTR_UIDGID) { readInt(); readInt(); }
      if (flags & SSH_FILEXFER_ATTR_PERMISSIONS) { readInt();}
      if (flags & SSH_FILEXFER_ATTR_ACMODTIME) {readInt(); readInt(); }
      if (flags & SSH_FILEXFER_ATTR_EXTENDED)
      {
        int count = readInt();
        for (int i=0; i<count; ++i)
        {
          readString();
          readString();
        }
      }
    }

    SFTP::SFTP(std::string const& address, std::string const& path)
      : _in(reactor::scheduler().io_service())
      , _out(reactor::scheduler().io_service())
      ,_server_address(address)
      , _path(path)
      , _sem(1)
      , _req(1000)
    {
      _connect();
    }

    void
    pipe(int pipefd[2])
    {
      if (::pipe(pipefd))
        throw elle::Error(elle::sprintf(
                            "unable to create pipe: %s", strerror(errno)));
    }

    void SFTP::_connect()
    {
      const char* args[5] = {"ssh", _server_address.c_str(), "-s", "sftp", 0};
      int out[2], in[2]; // read, write
      pipe(out);
      pipe(in);
      pid_t child = fork();
      if (child == 0)
      {
        close(in[0]);
        close(out[1]);
        dup2(in[1], 1);
        dup2(out[0], 0);
        execvp("ssh", (char* const*) args);
        std::cerr << "EXECVE EXIT" << std::endl;
        exit(0);
      }
      else
      {
        _child = child;
        _fout = out[1];
        _fin = in[0];
        _in.assign(_fin);
        _in.non_blocking(true);
        _out.assign(_fout);
        _out.non_blocking(true);
        new std::thread([child] {
            int status;
            ::waitpid(child, &status, 0);
            ELLE_WARN("Ssh process terminated");
        });
        Packet p;
        p.make(SSH_FXP_INIT, 3);
        ELLE_TRACE("Sending header: %x", p);
        p.writeTo(_out);
        ELLE_TRACE("Reading result...");
        p.readFrom(_in);
        int type = p.readByte();
        ELLE_TRACE("Got reply, len %s, type %s", p.size(), type);
        p.make(SSH_FXP_MKDIR, ++_req, _path, 0);
        ELLE_TRACE("Sending request: %x", p);
        p.writeTo(_out);
        ELLE_TRACE("waiting result...");
        p.readFrom(_in);
        type = p.readByte();
        int id = p.readInt();
        ELLE_TRACE("Got reply, len %s, type %s id %s", p.size(), type, id);
      }
    }
    elle::Buffer
    SFTP::_get(Key k) const
    {
      /*reactor::Lock lock(_sem);*/
      ELLE_TRACE("_get %x", k);
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      int req = ++_req;
      p.make(SSH_FXP_OPEN, req, path, SSH_FXF_READ, 0);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
      }
      ELLE_TRACE("got open answer: %x", p);
      try
      {
        p.expectType(SSH_FXP_HANDLE);
      }
      catch(PacketError const&)
      {
        throw infinit::storage::MissingKey(k);
      }
      int id = p.readInt();
      ELLE_ASSERT_EQ(id, req);
      elle::ConstWeakBuffer ch = p.readString();
      std::string handle = ch.string();
      elle::Buffer res;
      while (true)
      {
        int req = ++_req;
        p.make(SSH_FXP_READ, req, handle, 0, res.size(), 1000000000);
        {
          reactor::Lock lock(_sem);
          p.writeTo(_out);
          p.readFrom(_in);
        }
        try
        {
          p.expectType(SSH_FXP_DATA); // id data
        }
        catch(PacketError const& e)
        { // read on a 0-byte file causes an error
          p.make(SSH_FXP_CLOSE, ++_req, handle);
          {
            reactor::Lock lock(_sem);
            p.writeTo(_out);
            p.readFrom(_in);
          }
          if (e.erc() == SSH_FX_EOF)
            return res; // empty data, not an error
          else
            throw e;
        }
        int id = p.readInt();
        ELLE_ASSERT_EQ(id, req);
        elle::ConstWeakBuffer buf = p.readString();
        if (buf.size() == 0)
          break;
        res.append(buf.contents(), buf.size());
      }
      // close
      ELLE_TRACE("Closing");
      req = ++_req;
      p.make(SSH_FXP_CLOSE, req, handle);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
        p.readByte();
        int id = p.readInt();
        ELLE_ASSERT_EQ(id, req);
      }
      return res;
    }
    void
    SFTP::_erase(Key k)
    {
      /*reactor::Lock lock(_sem);*/
      ELLE_TRACE("_erase %x", k);
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      int req = ++_req;
      p.make(SSH_FXP_REMOVE, req, path);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
        p.readByte();
        int id = p.readInt();
        ELLE_ASSERT_EQ(id, req);
      }
    }
    void
    SFTP::_set(Key k, elle::Buffer const& value_, bool insert, bool update)
    {
      elle::Buffer value(value_.contents(), value_.size());
      /*reactor::Lock lock(_sem);*/
      ELLE_TRACE("_set %x of size %s", k, value.size());
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      int req = ++_req;
      p.make(SSH_FXP_OPEN, req, path,
             SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC,
             0);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
      }
      p.expectType(SSH_FXP_HANDLE);
      int id = p.readInt();
      ELLE_ASSERT_EQ(id, req);
      std::string handle = p.readString().string();
      ELLE_TRACE("got handle %x", handle);
      static const int block_size = 16384;
      if (value.size())
      {
        // Sending too big values freeezes things up, probably because it
        // fills the pipe with ssh
        for(int o = 0; o < 1 + int(value.size() - 1) / block_size; ++o)
        {
          ELLE_TRACE("write block %s", o);
          int req = ++_req;
          p.make(SSH_FXP_WRITE, req, handle, 0, o*block_size,
            elle::ConstWeakBuffer(
              value.contents() + o*block_size,
              std::min(block_size, int(value.size()) - o * block_size)));
          reactor::Lock lock(_sem);
          p.writeTo(_out);
          p.readFrom(_in);
          p.readByte();
          int id = p.readInt();
          ELLE_ASSERT_EQ(id, req);
        }
      }
      ELLE_TRACE("closing");
      //close
      req = ++_req;
      p.make(SSH_FXP_CLOSE, req, handle);
      reactor::Lock lock(_sem);
      p.writeTo(_out);
      p.readFrom(_in);
      p.readByte();
      id = p.readInt();
      ELLE_ASSERT_EQ(id, req);
    }
    std::vector<Key>
    SFTP::_list()
    {
      std::vector<Key> res;
      Packet p;
      int req = ++_req;
      p.make(SSH_FXP_OPENDIR, req, _path);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
      }
      p.expectType(SSH_FXP_HANDLE);
      int id = p.readInt();
      ELLE_ASSERT_EQ(id, req);
      std::string handle = p.readString().string();
      ELLE_TRACE("got handle %x", handle);

      while (true)
      {
        int req = ++_req;
        p.make(SSH_FXP_READDIR, req, handle);
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
        int type = p.readByte();
        if (type == SSH_FXP_STATUS)
          break;
        int id = p.readInt();
        (void)id;
        int count = p.readInt();
        for (int i=0; i<count; ++i)
        {
          std::string s = p.readString().string();
          p.skipAttr();
          if (s.substr(0, 2) != "0x" || s.length()!=66)
            continue;
          Key k = Key::from_string(s.substr(2));
          res.push_back(k);
        }
      }
      p.make(SSH_FXP_CLOSE, ++_req, handle);
      {
        reactor::Lock lock(_sem);
        p.writeTo(_out);
        p.readFrom(_in);
      }
      return res;
    }

    struct SFTPStorageConfig:
    public StorageConfig
    {
    public:
      std::string host;
      std::string path;
      SFTPStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("host", this->host);
        s.serialize("path", this->path);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::SFTP>(host, path);
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<SFTPStorageConfig>
    _register_SFTPStorageConfig("sftp");
  }
}
