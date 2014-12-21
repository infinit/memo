#include <unistd.h>
#include <arpa/inet.h>

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

      void readFrom(int fd);
      void writeTo(int fd);

      unsigned char readByte();
      int readInt();
      elle::ConstWeakBuffer readString();
      void expectType(int t); // eats the type

      void resetRead();
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
    void Packet::readFrom(int fd)
    {
      _pos = 0;
      size(4);
      int count = ::read(fd, contents(), 4);
      ELLE_ASSERT(count == 4);
      int len = readInt();
      size(4 + len);
      int pos = 0;
      ELLE_DEBUG("Reading new packet payload, size %s", len);
      while (pos < len)
      {
        int count = ::read(fd, contents() + 4 + pos, len - pos);
        if (count < 0)
          throw std::runtime_error("Error while reading");
        if (count == 0 && pos < len)
          throw std::runtime_error("Short read");
        pos += count;
      }
    }
    void Packet::writeTo(int fd)
    {
      ELLE_DEBUG("Writing packet %x", *this);
      unsigned int pos = 0;
      while (pos < size())
      {
        int count = ::write(fd, contents() + pos, size() - pos);
        if (count < 0)
          throw std::runtime_error(std::string("Error while writing ") + strerror(errno));
        if (count == 0 && pos < size())
          throw std::runtime_error("Short read");
        pos += count;
      }
      pos = 0;
      while (pos < _payload.size())
      {
        int count = ::write(fd, _payload.contents() + pos, _payload.size() - pos);
        if (count < 0)
          throw std::runtime_error(std::string("Error while writing ") + strerror(errno));
        if (count == 0 && pos < _payload.size())
          throw std::runtime_error("Short read");
        pos += count;
      }
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

    SFTP::SFTP(std::string const& address, std::string const& path)
      : _server_address(address)
      , _path(path)
    {
      _connect();
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
        _out = out[1];
        _in = in[0];
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
      ELLE_TRACE("_get %x", k);
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      p.make(SSH_FXP_OPEN, ++_req, path, SSH_FXF_READ, 0);
      p.writeTo(_out);
      p.readFrom(_in);
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
      ELLE_ASSERT_EQ(id, _req);
      elle::ConstWeakBuffer ch = p.readString();
      std::string handle = ch.string();
      p.make(SSH_FXP_READ, ++_req, handle, 0, 0, 1000000000);
      p.writeTo(_out);
      p.readFrom(_in);
      try
      {
        p.expectType(SSH_FXP_DATA); // id data
      }
      catch(PacketError const& e)
      { // read on a 0-byte file causes an error
        p.make(SSH_FXP_CLOSE, ++_req, handle);
        p.writeTo(_out);
        p.readFrom(_in);
        if (e.erc() == SSH_FX_EOF)
          return elle::Buffer(); // empty data, not an error
        else
          throw e;
      }
      p.readInt();
      elle::ConstWeakBuffer buf = p.readString();
      elle::Buffer res(buf.contents(), buf.size());
      // close
      ELLE_TRACE("Closing");
      p.make(SSH_FXP_CLOSE, ++_req, handle);
      p.writeTo(_out);
      p.readFrom(_in);
      return res;
    }
    void
    SFTP::_erase(Key k)
    {
      ELLE_TRACE("_erase %x", k);
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      p.make(SSH_FXP_REMOVE, ++_req, path);
      p.writeTo(_out);
      p.readFrom(_in);
    }
    void
    SFTP::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_TRACE("_set %x of size %s", k, value.size());
      std::string path = elle::sprintf("%s/%x", _path, k);
      Packet p;
      p.make(SSH_FXP_OPEN,++_req, path,
             SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC,
             0);
      p.writeTo(_out);
      p.readFrom(_in);
      p.expectType(SSH_FXP_HANDLE);
      int id = p.readInt();
      ELLE_ASSERT_EQ(id, _req);
      std::string handle = p.readString().string();
      ELLE_TRACE("got handle %x (%s)", handle, handle);
      static const int block_size = 32768;
      if (value.size())
      {
        // Sending too big values freeezes things up, probably because it
        // fills the pipe with ssh
        for(int o=0; o < 1 + (value.size()-1)/block_size; ++o)
        {
          ELLE_TRACE("write block %s", o);
          p.make(SSH_FXP_WRITE, ++_req, handle, 0, o*block_size,
            elle::ConstWeakBuffer(value.contents() + o*block_size,
              std::min(block_size, (int)value.size() - o*block_size)));
          p.writeTo(_out);
          p.readFrom(_in);
        }
      }
      /*
      p.make(SSH_FXP_WRITE, ++_req, handle, 0, 0, value);
      p.writeTo(_out);
      p.readFrom(_in);
      */
      ELLE_TRACE("closing");
      //close
      p.make(SSH_FXP_CLOSE, ++_req, handle);
      p.writeTo(_out);
      p.readFrom(_in);
    }
  }
}