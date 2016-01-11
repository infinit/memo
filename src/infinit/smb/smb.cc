# include <infinit/smb/smb.hh>
# include <reactor/network/tcp-socket.hh>
# include <reactor/Scope.hh>
#include <elle/utility/Move.hh>
#include <elle/log.hh>

#include <cstdint>

ELLE_LOG_COMPONENT("infinit.smb");
#include <sys/stat.h>

#ifdef INFINIT_WINDOWS
#include <fcntl.h>
#include <ntstatus.h>
#undef stat
#endif



namespace infinit
{
  namespace smb
  {

    static uint64_t time_to_filetime(time_t v)
    {
      uint64_t fnow = (uint64_t)v * 10000000ULL;
      fnow += 116444736000000000ULL;
      return fnow;
    }
    static std::string to_utf16(std::string const& s)
    {
      std::string res;
      for (int i=0; i<signed(s.size()); ++i)
      {
        res += s[i];
        res += (char)0;
      }
      return res;
    }

    class Writer
    {
    public:
      Writer(std::ostream& os)
      : os(os)
      , _off(0)
      {}
      Writer& w8(uint8_t v)   { _off += 1;os.write((const char*)&v, 1); return *this; }
      Writer& w16(uint16_t v) { _off += 2;os.write((const char*)&v, 2); return *this; }
      Writer& w32(uint32_t v) { _off += 4;os.write((const char*)&v, 4); return *this; }
      Writer& w64(uint64_t v) { _off += 8;os.write((const char*)&v, 8); return *this; }
      Writer& w(const char* data, int sz) { _off+=sz; os.write(data, sz); return *this;}
      uint64_t offset() { return _off;}
      template<typename T> Writer& ws(const T& t)
      {
        return w((const char*)(const void*)&t, sizeof(t));
      }
      Writer& whex(const char* hex)
      {
        int p = 0;
        while (hex[p])
        {
          char v[3] = {hex[p], hex[p+1], 0};
          unsigned char val = strtoul(v, 0, 16);
          w8(val);
          p+= 2;
        }
        return *this;
      }
      std::ostream& os;
      uint64_t _off;
    };
    enum SMBCommand
    {
      SMB2_NEGOTIATE = 0x0000,
      SMB2_SESSION_SETUP = 0x0001,
      SMB2_LOGOFF = 0x0002,
      SMB2_TREE_CONNECT = 0x0003,
      SMB2_TREE_DISCONNECT = 0x0004,
      SMB2_CREATE = 0x0005,
      SMB2_CLOSE = 0x0006,
      SMB2_FLUSH = 0x0007,
      SMB2_READ = 0x0008,
      SMB2_WRITE = 0x0009,
      SMB2_LOCK = 0x000A,
      SMB2_IOCTL = 0x000B,
      SMB2_CANCEL = 0x000C,
      SMB2_ECHO = 0x000D,
      SMB2_QUERY_DIRECTORY = 0x000E,
      SMB2_CHANGE_NOTIFY = 0x000F,
      SMB2_QUERY_INFO = 0x0010,
      SMB2_SET_INFO = 0x0011,
      SMB2_OPLOCK_BREAK = 0x0012
    };
#ifndef INFINIT_WINDOWS
    enum CreateOptions
    {
      FILE_DIRECTORY_FILE = 1,
      FILE_NON_DIRECTORY_FILE = 0x40,
      FILE_DELETE_ON_CLOSE = 0x00001000,
    };
    enum CreateDisposition
    {
      FILE_SUPERSEDE = 0,
      FILE_OPEN = 1,
      FILE_CREATE = 2,
      FILE_OPEN_IF = 3,
      FILE_OVERWRITE = 4,
      FILE_OVERWRITE_IF = 5,
    };

    enum NTStatus
    {
      STATUS_FILE_IS_A_DIRECTORY = 0xC00000BA,
      STATUS_OBJECT_NAME_COLLISION = 0xC0000035,
      STATUS_NO_SUCH_FILE = 0xC000000F,
      STATUS_NOT_A_DIRECTORY = 0xC0000103,
      STATUS_NO_MORE_FILES = 0x80000006,
      STATUS_NOT_IMPLEMENTED = 0xC0000002,
      STATUS_IO_DEVICE_ERROR = 0xC0000185,
      STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034,
      STATUS_OBJECT_PATH_NOT_FOUND = 0xC000003a,
      STATUS_FILE_CLOSED = 0xC0000128,
      STATUS_CANCELLED = 0xC0000120,
      STATUS_PENDING = 0x103,
      STATUS_FS_DRIVER_REQUIRED = 0xC000019c,
      STATUS_DIRECTORY_NOT_EMPTY = 0xC0000101,
    };
#endif
    enum FileInformationClass
    {
      FileDirectoryInformation = 0x1,
      FileFullDirectoryInformation = 0x2,
      FileIdFullDirectoryInformation = 0x26,
      FileBothDirectoryInformation = 0x3,
      FileIdBothDirectoryInformation = 0x25,
      FileNamesInformation = 0x0C,
      FileAccessInformation = 8,
      FileAllInformation = 18,
      FileBasicInformation = 4,
      FileEndOfFileInformation  = 20,
      FileRenameInformation = 10,
      FileModeInformation = 16,
      FileAllocationInformation = 19,
    };
    struct SMB2Header
    {
      uint8_t protocolId[4];
      uint16_t structSize;
      uint16_t creditCharge;
      uint32_t ntstatus;
      uint16_t command;
      uint16_t credit;
      uint32_t flags;
      uint32_t nextCommand; // offset of next header in compound
      uint64_t messageId;   // uid on the connection
      uint32_t reserved1;
      uint32_t treeId;
      uint64_t sessionId;
      uint8_t  signature[16];
    }__attribute__((packed)) ;

    struct SMB2Create
    {
      uint16_t size;
      uint8_t securityFlags;
      uint8_t opLockLevel;
      uint32_t impersonationLevel;
      uint64_t smbCreateFlags; // reserved
      uint64_t reserved;
      uint32_t desiredAccess;
      uint32_t fileAttributes;
      uint32_t shareAccess;
      uint32_t createDispositions;
      uint32_t createOptions;
      uint16_t nameOffset;
      uint16_t nameLength;
      uint32_t createContextOffset;
      uint32_t createContextLength;
    } __attribute__((packed)) ;

    class Reader
    {
    public:
      Reader(SMB2Header const& h)
      : _d((const unsigned char*)(const void*)&(&h)[1])
      {
      }
      Reader(const char* d)
      : _d((const unsigned char*)d)
      {}
      Reader& skip(int amount)
      {
        _d += amount;
        return *this;
      }
      Reader& r8(uint8_t& v) { v = _d[0]; ++_d; return *this;}
      Reader& r16(uint16_t& v) { v =*(uint16_t*)_d; _d += 2; return *this;}
      Reader& r32(uint32_t& v) { v =*(uint32_t*)_d; _d += 4; return *this;}
      Reader& r64(uint64_t& v) { v =*(uint64_t*)_d; _d += 8; return *this;}

      const unsigned char* _d;
    };

    class SMBServer;
    class SMBConnection
    {
    public:
      SMBConnection(SMBServer& server,
                    std::unique_ptr<reactor::network::Socket> socket)
      : _server(server)
      , _socket(std::move(socket))
      , _next_file_id(1)
      , _next_directory_id(_directory_start+1)
      , _sstate(0)
      {
        this->_serve_thread = elle::make_unique<reactor::Thread>(
          elle::sprintf("%s server", *this),
          [this] { this->_serve(); });
      }
      void _serve();
      void _process(SMB2Header* h);
      void send_negotiate_reply();
      void send_session_setup_reply(SMB2Header* h);
      void tree_connect(SMB2Header* h);
      void tree_disconnect(SMB2Header* h);
      void query_directory(SMB2Header* h);
      void create(SMB2Header* h);
      void echo(SMB2Header* h);
      void close(SMB2Header* h);
      void query_info(SMB2Header* h);
      void set_info(SMB2Header* h);
      void read(SMB2Header* h);
      void write(SMB2Header* h);
      void logoff(SMB2Header* h);
      void notify(SMB2Header* h);
      void error(SMB2Header* h, int code, int payloadlen=0);
      SMBServer& _server;
      std::unique_ptr<reactor::network::Socket> _socket;
      std::unique_ptr<reactor::Thread> _serve_thread;
      struct DirInfo
      {
        std::string glob;
        std::string name;
        std::string full_path;
        std::shared_ptr<reactor::filesystem::Path> directory;
        std::vector<std::pair<std::string, struct stat>> content;
        int offset;
        bool deleteOnClose;
      };
      struct FileInfo
      {
        std::string name;
        std::string full_path;
        std::unique_ptr<reactor::filesystem::Handle> handle;
        std::shared_ptr<reactor::filesystem::Path> file;
        bool deleteOnClose;
      };
      std::unordered_map<uint64_t, FileInfo> _file_handles;
      std::unordered_map<uint64_t, DirInfo> _dir_handles;
      uint64_t _next_file_id;
      uint64_t _next_directory_id;
      static const uint64_t _directory_start = 0x8000000000000000ULL;
      int _sstate;
    };


    elle::Buffer make_reply(SMB2Header const& hin,
                            std::function<void (Writer& w)> f)
    {
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin.messageId;
      h.sessionId = (uint64_t)hin.sessionId;
      h.command = hin.command;
      h.treeId = hin.treeId;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        f(w);
        ios.flush();
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      return std::move(buf);
    }

    SMBServer::SMBServer(std::unique_ptr<infinit::filesystem::FileSystem> fs)
    : _fs(new reactor::filesystem::FileSystem(std::move(fs), true))
    {
      this->_server = elle::make_unique<reactor::network::TCPServer>();
      this->_server->listen(445);
      this->_server_thread = elle::make_unique<reactor::Thread>(
        elle::sprintf("%s server", *this),
        [this] { this->_serve(); });
    }
    void SMBServer::_serve()
    {
      ELLE_LOG("serving");
      elle::With<reactor::Scope>() << [this] (reactor::Scope& scope)
        {
          while (true)
          {
            auto socket = elle::utility::move_on_copy(this->_server->accept());
            _connections.insert(elle::make_unique<SMBConnection>(*this, std::move(*socket)));

          }
        };
    }
    void SMBConnection::send_session_setup_reply(SMB2Header* hin)
    {
      uint16_t secBufOffset;
      Reader(*hin).skip(12).r16(secBufOffset);
      const char* secBuf = (char*)hin + secBufOffset;
      bool raw = !memcmp("NTLMSSP", secBuf, 7);

      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin->messageId;
      h.sessionId = (uint64_t)this;
      h.command = SMB2_SESSION_SETUP;
      if (_sstate == 0)
        h.ntstatus = 0xc0000016;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        if (_sstate == 1)
        {
          w.w16(9).w16(3);
          w.w16(0x48).w16(9)
          .whex("a1073005a0030a0100");
        }
        else
        {
          w.w16(9).w16(0);
          if (!raw)
          {
            // version for windows
            w.w16(0x48).w16(145)
            .whex("a1818e30818ba0030a0101a10c060a2b06010401823702020aa27604744e544c4d53535000020000000a000a003800000015828ae2cf79b9ee3acdfb4200000000000000003200320042000000060100000000000f440045004e004500420002000a00440045004e004500420001000a00440045004e00450042000400000003000a00640065006e006500620000000000");
          }
          else
          {
            // version for smbmount
            w.w16(0x48).w16(108)
            .whex("4e544c4d53535000020000000a000a003000000005028aa01c6dedee861eda0c0000000000000000320032003a000000440045004e004500420002000a00440045004e004500420001000a00440045004e00450042000400000003000a00640065006e006500620000000000");
          }
          // version for smbclient
          //w.w16(0x48).w16(137)
          //.whex("a18186308183a0030a0101a10c060a2b06010401823702020aa26e046c4e544c4d53535000020000000a000a003000000015828a60d820b1baa672ee5c0000000000000000320032003a000000440045004e004500420002000a00440045004e004500420001000a00440045004e00450042000400000003000a00640065006e006500620000000000");
        }
        ++_sstate;
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("session %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::send_negotiate_reply()
    {
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      elle::Buffer buf;
      {
        uint64_t fnow = time_to_filetime(time(0));
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h).w16(0x41).w16(1).w16(0x210).w16(0).w64(1).w64(0) //guid
        .w32(0x5) // caps
        .w32(1048576).w32(1048576).w32(1048576) // sz
        .w64(fnow).w64(0) // time, boottime
        .w16(0x80).w16(74) // sec buffer pos/len
        .w32(0)
        .whex("604806062b0601050502a03e303ca00e300c060a2b06010401823702020aa32a3028a0261b246e6f745f646566696e65645f696e5f5246433431373840706c656173655f69676e6f7265")
        ;
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("negotiate %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::_serve()
    {
      auto& s = *_socket;
      try
      {
        while (true)
        {
          ELLE_LOG("reading pcket");
          auto nb = s.read(4);
          int sz = nb[3] + (nb[2]<<8) + (nb[1] << 16);
          ELLE_ASSERT(sz < 10000000);
          ELLE_LOG("reading payload %s", sz);
          auto packet = s.read(sz);
          auto data = packet.contents();
          ELLE_LOG("processing packet");
          ELLE_ASSERT_EQ(data[1], 'S');
          ELLE_ASSERT_EQ(data[2], 'M');
          ELLE_ASSERT_EQ(data[3], 'B');
          if (data[0] == 0xFF)
          { // SMB1 packet
            ELLE_ASSERT_EQ(data[4], 0x72); // SMB1 negotiate
            send_negotiate_reply();
          }
          else
          {
            SMB2Header* h = (SMB2Header*)data;
            while (h)
            {
              ELLE_ASSERT_EQ((int)h->protocolId[0], 0xFE);
              _process(h);
              if (h->nextCommand)
              {
                ELLE_LOG("nextCommand %s", (int)h->nextCommand);
                h = (SMB2Header*)(((const char*)h) + (int)h->nextCommand);
              }
              else
                h = nullptr;
            }
          }
        }
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("exception %s", e.what());
      }
      _socket->close();
      _serve_thread->terminate_now();
    }

    void SMBConnection::_process(SMB2Header* h)
    {
      ELLE_LOG("command %s", (int)h->command);
      switch ((SMBCommand)h->command)
      {
      case SMB2_NEGOTIATE:
        send_negotiate_reply();
        break;
      case SMB2_SESSION_SETUP:
        send_session_setup_reply(h);
        break;
      case SMB2_LOGOFF:
        logoff(h);
        break;
      case SMB2_TREE_CONNECT:
        tree_connect(h);
        break;
      case SMB2_TREE_DISCONNECT:
        tree_disconnect(h);
        break;
      case SMB2_CREATE:
        create(h);
        break;
      case SMB2_CLOSE:
        close(h);
        break;
      case SMB2_READ:
        read(h);
        break;
      case SMB2_WRITE:
        write(h);
        break;
      case SMB2_IOCTL:
        error(h, STATUS_FS_DRIVER_REQUIRED, 9);
        break;
      case SMB2_CANCEL:
        // FIXME: wrong, command should be the one of op being cancelled
        error(h, STATUS_CANCELLED, 9);
        break;
      case SMB2_FLUSH:
      case SMB2_LOCK:

      case SMB2_OPLOCK_BREAK:
        error(h, STATUS_NOT_IMPLEMENTED);
        break;
      case SMB2_ECHO:
        echo(h);
        break;
      case SMB2_QUERY_DIRECTORY:
        query_directory(h);
        break;
      case SMB2_CHANGE_NOTIFY:
        notify(h);
        break;
      case SMB2_QUERY_INFO:
        query_info(h);
        break;
      case SMB2_SET_INFO:
        set_info(h);
        break;
      }
    }

    static std::string from_utf16(const char* data, int len)
    {
      std::string res;
      for (int i=0; i<len; i+=2)
        res += data[i];
      return res;
    }
    void SMBConnection::tree_connect(SMB2Header* hin)
    {
      unsigned char* d = (unsigned char*) hin;
      int poffset = d[68] + (d[69] << 8);
      int plen = d[70] + (d[71] << 8);
      ELLE_LOG("path at %s/%s", poffset, plen);
      std::string path = from_utf16((char*)d+poffset, plen);
      ELLE_LOG("path %s", path);
      path = path.substr(path.find_last_of('\\')+1);
      int share_type;
      if (path == "IPC$")
        share_type = 2; // named pipe
      else
        share_type = 1; // physical disk
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin->messageId;
      h.sessionId = (uint64_t)this;
      h.command = SMB2_TREE_CONNECT;
      h.treeId = share_type;
      h.messageId = hin->messageId;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        w.w16(0x10); // sz
        w.w8(share_type);
        w.w8(0);
        w.w32(0).w32(0); //shareflags capabilities
        if (share_type == 1)
          w.w32(0x001f00ff);
        else
          w.w32(0x001f00a9); // mask
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("connect %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::tree_disconnect(SMB2Header* hin)
    {
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin->messageId;
      h.sessionId = (uint64_t)this;
      h.command = SMB2_TREE_DISCONNECT;
      h.treeId = hin->treeId;
      h.messageId = hin->messageId;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        w.w16(4); // sz
        w.w16(0);
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("disconnect %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::echo(SMB2Header* hin)
    {
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin->messageId;
      h.sessionId = (uint64_t)this;
      h.command = SMB2_ECHO;
      h.treeId = hin->treeId;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        w.w16(4); // sz
        w.w16(0);
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("echo %s", buf.size());
      _socket->write(buf);
    }

    void SMBConnection::create(SMB2Header* hin)
    {
      SMB2Create* c = (SMB2Create*)(void*)&hin[1];
      char* d = (char*)(void*)hin;
      std::string path = from_utf16(d + c->nameOffset, c->nameLength);
      std::replace( path.begin(), path.end(), '\\', '/');
      ELLE_LOG("create %s", path);
      bool exists = false;
      bool isdir = false;
      struct stat st;
      std::shared_ptr<reactor::filesystem::Path> entry;
      try
      {
        entry = _server._fs->path(path);
      }
      catch (reactor::filesystem::Error const& e)
      {
        return error(hin, STATUS_OBJECT_PATH_NOT_FOUND, 89);
      }
      try
      {
        entry->stat(&st);
        exists = true;
        isdir = S_ISDIR(st.st_mode);
      }
      catch (reactor::filesystem::Error const& e)
      {}
      catch (std::exception const& e)
      {
        ELLE_LOG("non rfs exception: %s", e.what());
      }
      int cdisp = c->createDispositions;
      int copt = c->createOptions;
      bool deleteOnClose = copt & FILE_DELETE_ON_CLOSE;
      ELLE_LOG("cdisp %s  copt %s  caccess %s", cdisp, copt, (int)c->desiredAccess);
      if ((copt & FILE_NON_DIRECTORY_FILE) && isdir)
        return error(hin, STATUS_FILE_IS_A_DIRECTORY, 89);
      if (exists && cdisp == FILE_CREATE)
        return error(hin, STATUS_OBJECT_NAME_COLLISION, 89);
      if (!exists && (cdisp == FILE_OVERWRITE || cdisp == FILE_OPEN))
        return error(hin, STATUS_OBJECT_NAME_NOT_FOUND, 89);
      if ((copt & FILE_DIRECTORY_FILE) && exists && !isdir && cdisp == FILE_CREATE)
        return error(hin, STATUS_OBJECT_NAME_COLLISION, 89);
      if ((copt & FILE_DIRECTORY_FILE) && exists && !isdir)
        return error(hin, STATUS_NOT_A_DIRECTORY, 89);
      std::unique_ptr<reactor::filesystem::Handle> handle;
      int createAction = 0;
      if (!exists)
      {
        createAction = 2; // FILE_CREATED
        if (copt & FILE_DIRECTORY_FILE)
        {
          ELLE_LOG("mkdir");
          entry->mkdir(0777);
          entry = _server._fs->path(path);
          entry->stat(&st);
          isdir = true;
        }
        else
        {
          ELLE_LOG("mkfile");
          int flags = O_CREAT;
          if ((c->desiredAccess & 0x02) || (c->desiredAccess & 0x40000000))
            flags |= O_RDWR;
          else
            flags |= O_RDONLY;
          handle = entry->create(flags, 0777 | S_IFREG);
          entry = _server._fs->path(path);
          entry->stat(&st);
        }
      }
      else
      {
        if (!isdir)
        {
          int flags = 0;
          if ((c->desiredAccess & 0x02) || (c->desiredAccess & 0x40000000))
            flags |= O_RDWR;
          else
            flags |= O_RDONLY;
          if (cdisp != FILE_OPEN && cdisp != FILE_OPEN_IF)
          {
            createAction = 3; // FILE_OVERWRITTEN
            flags |= O_TRUNC;
          }
          else
            createAction = 1; // FILE_OPENED
          handle = entry->open(flags, 0777);
          entry = _server._fs->path(path);
        }
        else
          createAction = 1; // FILE_OPENED
      }
      uint64_t guid = 0;
      std::string name = boost::filesystem::path(path).filename().string();
      if (handle)
      {
        guid = ++_next_file_id;
        _file_handles[guid] = FileInfo{name, path, std::move(handle), entry, deleteOnClose};
      }
      else
      {
        guid = ++ _next_directory_id;
        _dir_handles[guid] = DirInfo{"", name, path, entry, {}, -1, deleteOnClose};
      }

      elle::Buffer buf = make_reply(*hin,
        [&](Writer& w) {
          w.w16(89).w8(1).w8(0);
          w.w32(createAction);
          w.w64(0).w64(0).w64(0).w64(0); // times
          w.w64(st.st_size).w64(st.st_size);
          w.w32(isdir? 0x10 : 0x80);
          w.w32(0); // reserved
          w.w64(0).w64(guid);
          w.w32(0).w32(0); // context
        });
      ELLE_LOG("create %s", buf.size());
      _socket->write(buf);
    }

    void SMBConnection::error(SMB2Header* hin, int erc, int payloadlen)
    {
      SMB2Header h;
      memset(&h, 0, sizeof(h));
      h.protocolId[0] = 0xFE;
      h.protocolId[1] = 'S';
      h.protocolId[2] = 'M';
      h.protocolId[3] = 'B';
      h.structSize = 64;
      h.flags = 1;
      h.credit = 1;
      h.messageId = hin->messageId;
      h.sessionId = (uint64_t)this;
      h.command = hin->command;
      h.treeId = hin->treeId;
      h.ntstatus = erc;
      elle::Buffer buf;
      {
        elle::IOStream ios(buf.ostreambuf());
        Writer w(ios);
        w.w32(0); // nbs
        w.ws(h);
        w.w16(payloadlen);
        for (int i=0; i<payloadlen-2; ++i)
          w.w8(0);
      }
      uint32_t sz = buf.size() - 4;
      auto data = buf.mutable_contents();
      data[1] = sz >> 16;
      data[2] = sz >> 8;
      data[3] = sz;
      ELLE_LOG("error %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::close(SMB2Header* hin)
    {
      uint64_t guid;
      uint16_t flags;
      Reader(*hin).skip(2).r16(flags).skip(4).skip(8).r64(guid);
      ELLE_LOG("close %s  flags %s", guid, flags);
      // FIXME: stat in reply
      // FIXME: error check
      if (guid >= _directory_start)
      {
        auto& e = _dir_handles.at(guid);
        if (e.deleteOnClose)
        {
          try
          {
            e.directory->rmdir();
          }
          catch(reactor::filesystem::Error const& e)
          {
            ELLE_LOG("delete: %s", e.what());
            return error(hin, STATUS_DIRECTORY_NOT_EMPTY, 60);
          }
        }
        _dir_handles.erase(guid);
      }
      else
      {
        auto& e = _file_handles.at(guid);
        e.handle->close();
        if (e.deleteOnClose)
          e.file->unlink();
        _file_handles.erase(guid);
      }
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(60).w16(flags).w32(0)
          .w64(0).w64(0).w64(0).w64(0).w64(0).w64(0) // times and size
          .w32(0) // attrs
          ;
      });
      ELLE_LOG("close %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::query_directory(SMB2Header* hin)
    {
      // FIXME: honor index
      // FIXME: honor SINGLE_ENTRY
      // FIXME: honor max buffer size
      uint8_t fclass, flags;
      uint64_t guid;
      uint32_t index;
      uint16_t nameOffset, nameLength;
      Reader r(*hin);
      r.skip(2).r8(fclass).r8(flags).r32(index).skip(8)
      .r64(guid).r16(nameOffset).r16(nameLength);
      const char* data = (const char*)(const void*)hin + nameOffset;
      std::string glob = from_utf16(data, nameLength);
      ELLE_LOG("querydict guid %s  fclass %s  flags %s  index %s glob(%s) '%s'",
               guid, (int)fclass, (int)flags, index, glob.size(), glob);
      auto it = _dir_handles.find(guid);
      if (it == _dir_handles.end())
        return error(hin, STATUS_FILE_CLOSED, 9);
      DirInfo& di = it->second;
      if ((flags & 0x1) || (flags & 0x10)) // RESTART_SCAN, REOPEN
        di.offset = -1;
      if ((flags & 0x1) && !di.glob.empty())
        glob = di.glob;
      di.glob = glob;
      bool exact = false;
      if (di.offset == -1)
      {
        di.offset = 0;
        di.content.clear();
        std::string pre, post;
        if (glob != "*")
        {
          auto p = glob.find('*');
          if (p == glob.npos)
          {
            pre = glob;
            exact = true;
          }
          else
          {
            pre = glob.substr(0, p);
            post = glob.substr(p+1);
          }
          ELLE_LOG("glob: pre '%s' post '%s' exact %s", pre, post, exact);
        }
        auto adder = [&](std::string const& n, struct stat* st)
        {
          if (exact)
          {
            if (pre != n)
              return;
          }
          else
          {
            if (!pre.empty() && (n.size() < pre.size() || n.substr(0, pre.size()) != pre))
              return;
            if (!post.empty() && (n.size() < post.size() || n.substr(n.size()-post.size()) != post))
              return;
          }
          ELLE_LOG("Adding %s to listing", n);
          di.content.push_back(std::make_pair(n, *st));
        };
        di.directory->list_directory(adder);
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = 0777 | S_IFDIR;
        adder(".", &st);
        adder("..", &st);
        std::sort(di.content.begin(), di.content.end(),
          [] (std::pair<std::string, struct stat> a,
             std::pair<std::string, struct stat> b) -> bool
          {
            return a.first < b.first;
          });
      }

      if (di.offset >= signed(di.content.size()))
        return error(hin, STATUS_NO_MORE_FILES, 9);
      // I'm in hell, factoring this is madness because of random paddings!
      /* layout:
      common: nextentry4 fileoffset4 ctime8 atime8 wtime8 chtime8 sz8 sz8 attr4 fnlength4
      FileDirectoryInformation: filenameVAR
      FileFullDirectoryInformation: easize4 fnVAR
      FileIdFullDirectoryInformation: easize4 res4 fileid8 fnVAR
      FileBothDirectoryInformation: easize4 snlen1 res1 sname24 fnVAR
      FileIdBothDirectoryInformation easize4 snlen1 res1 sname24 res2 fileid8 fnVAR
      FileNamesInformation:  nextentry4 fileoffset4 fnlen4 fnVAR
      */
      elle::Buffer payload;
      elle::IOStream ios(payload.ostreambuf());
      Writer wp(ios);
      for (int i = di.offset; i < signed(di.content.size()); ++i)
      {
        int nextEntryOffset = wp.offset();
        wp.w32(0).w32(0); // nextoffset  fileoffset
        if (fclass != FileNamesInformation)
        {
          struct stat& st = di.content[i].second;
          uint64_t atime = time_to_filetime(st.st_atime);
          uint64_t mtime = time_to_filetime(st.st_mtime);
          wp.w64(mtime).w64(atime).w64(mtime).w64(mtime);
          wp.w64(st.st_size).w64(st.st_size);
          wp.w32(S_ISDIR(st.st_mode) ? 0x10 : 0x80);
        }
        std::string const& fname = di.content[i].first;
        std::string wfn = to_utf16(fname);
        wp.w32(wfn.size());
        uint64_t fuid = 0;
        if (fclass == FileIdBothDirectoryInformation || fclass == FileIdFullDirectoryInformation)
        {
          std::string block;
          if (fname == ".")
            block = di.directory->getxattr("user.infinit.block");
          else if (fname == "..")
            block = "0x0000000000000000000000000000000000000000000000000000000000000000";
          else
          {
            auto fpath = di.directory->child(fname);
            block = fpath->getxattr("user.infinit.block");
          }
          ELLE_LOG("got block %s", block);
          auto addr = model::Address::from_string(block.substr(2));
          fuid = *(uint64_t*)addr.value();
        }
        if (fclass != FileDirectoryInformation && fclass != FileNamesInformation)
        {
          wp.w32(0); // easize
        }
        if (fclass == FileBothDirectoryInformation || fclass == FileIdBothDirectoryInformation)
        {
          wp.w8(0).w8(0).w64(0).w64(0).w64(0); // shortname
          if (fclass == FileIdBothDirectoryInformation)
            wp.w16(0).w64(fuid); // res fileid
        }
        if (fclass == FileIdFullDirectoryInformation)
        {
          wp.w32(0).w64(fuid); // res fileid
        }
        // and finally, the filename
        wp.w(wfn.data(), wfn.size());
        // set nextentryoffset
        if (i != signed(di.content.size()) - 1)
        {
          uint32_t len = wp.offset() - nextEntryOffset;
          while (len % 8)
          {
            wp.w8(0);
            len = wp.offset() - nextEntryOffset;
          }
          ios.flush();
          ELLE_ASSERT_EQ(payload.size(), wp.offset());
          *(uint32_t*)(payload.mutable_contents() + nextEntryOffset) = len;
        }
      }
      ios.flush();
      di.offset = di.content.size();
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(9).w16(64 + 8).w32(payload.size());
          w.w((const char*)payload.contents(), payload.size());
      });
      ELLE_LOG("querydict %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::query_info(SMB2Header* hin)
    {
      uint8_t infotype, infoclass;
      uint32_t eaflags, additionalinfo;
      uint64_t guid;
      Reader(*hin).skip(2).r8(infotype).r8(infoclass)
      .skip(12).r32(additionalinfo).r32(eaflags)
      .skip(8).r64(guid);
      ELLE_LOG("query_info guid %s  type %s  class %s", guid, (int)infotype, (int)infoclass);
      elle::Buffer payload;
      elle::IOStream stream(payload.ostreambuf());
      Writer w(stream);
      if (infotype == 0x02) // filesystem
      { // classes: https://msdn.microsoft.com/en-us/library/cc232100.aspx
        if (infoclass == 0x03) // FileFsSizeInformation
        {
          struct statvfs vfs;
          _server._fs->path("/")->statfs(&vfs);
          w.w64(vfs.f_blocks).w64(vfs.f_bfree).w32(1).w32(vfs.f_frsize);
        }
        else if (infoclass == 1) // FileFsVolumeInformation
        {
          w.w64(0).w32(0).w32(6).w8(0).w8(0);
          w.w("f\0o\0o\0", 6);
        }
        else if (infoclass == 5) // FileFsAttributeInformation
        {
          // xattr hardlink unicode preservecase casesensitive
          w.w32(0x00800000 | 0x00400000 | 0x00000004 |0x00000002 | 0x00000001)
          .w32(256) // maxnamelen
          .w32(6).w("f\0o\0o\0", 6);
        }
      }
      if (infotype == 0x01) // FILE_INFO
      {
        struct stat st;
        std::string name;
        try
        {
          if (guid >= _directory_start)
          {
            auto& e = _dir_handles.at(guid);
            e.directory->stat(&st);
            name = e.name;
          }
          else
          {
            auto& e = _file_handles.at(guid);
            e.file->stat(&st);
            name = e.name;
          }
        }
        catch (std::exception const& e)
        {
          error(hin, STATUS_FILE_CLOSED, 9);
        }
        // gn, there is no less than 18 values for infoclass...
        if (infoclass == 0x06) // INTERNAL
        {
          w.w64(0);
        }
        if (infoclass == 0x14) // EOF
        {
          w.w64(st.st_size);
        }
        if (infoclass == 13) // FILE_DISPOSITION_INFO
        {
          bool doc;
          if (guid >= _directory_start)
            doc = _dir_handles.at(guid).deleteOnClose;
          else
            doc = _file_handles.at(guid).deleteOnClose;
          w.w8(doc);
        }
        if (infoclass == 0x05) // STANDARD_INFO
        {
          w.w64(st.st_size).w64(st.st_size);
          w.w32(st.st_nlink).w8(0).w8(S_ISDIR(st.st_mode)).w16(0);
        }
        if (infoclass == 0x12) // ALL_INFO
        {
          uint64_t atime = time_to_filetime(st.st_atime);
          uint64_t mtime = time_to_filetime(st.st_mtime);
          //basic
          w.w64(mtime).w64(atime).w64(mtime).w64(mtime);
          w.w32(S_ISDIR(st.st_mode) ? 0x10 : 0x80);
          w.w32(0);
          //standard
          w.w64(st.st_size).w64(st.st_size);
          w.w32(st.st_nlink).w8(0).w8(S_ISDIR(st.st_mode)).w16(0);

          w.w64(0); //internal
          w.w32(0); // easize
          w.w32(0x101FF); //access
          w.w64(0); // current byte offset
          w.w32(0); // mode
          w.w32(0); // alignment
          if (name == "")
            name = "\\";
          std::string uname = to_utf16(name);
          w.w32(uname.size());
          w.w(uname.data(), uname.size());
        }
      }
      stream.flush();
      if (payload.empty())
      {
        return error(hin, STATUS_NOT_IMPLEMENTED, 9);
      }
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(9).w16(64 + 8).w32(payload.size());
          w.w((const char*)payload.contents(), payload.size());
      });
      ELLE_LOG("queryinfo %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::read(SMB2Header* hin)
    {
      uint64_t offset;
      uint32_t size;
      uint64_t guid;
      uint32_t mincount; // min read count or error

      Reader(*hin).skip(4).r32(size).r64(offset).skip(8).r64(guid).r32(mincount);
      ELLE_LOG("read %s bytes at %s on %s", size, offset, guid);
      auto& handle = _file_handles.at(guid).handle;
      elle::Buffer payload;
      payload.size(size);
      int nread = handle->read(payload, size, offset);
      if (nread < signed(mincount) || nread < 0)
        return error(hin, STATUS_IO_DEVICE_ERROR, 17);
      payload.size(nread);
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(17).w8(64+16).w8(0).w32(nread).w32(0).w32(0);
          w.w((const char*)payload.contents(), payload.size());
      });
      ELLE_LOG("read %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::write(SMB2Header* hin)
    {
      // FIXME: validate size
      uint16_t dataOffset;
      uint64_t offset;
      uint32_t size;
      uint64_t guid;
      Reader(*hin).skip(2).r16(dataOffset).r32(size).r64(offset).skip(8).r64(guid);
      const char* data = (const char*)(&hin[0]) + dataOffset;
      ELLE_LOG("write %s at %s on %s", size, offset, guid);
      auto& handle = _file_handles.at(guid).handle;
      int nw = handle->write(elle::WeakBuffer(const_cast<char*>(data), size), size, offset);
      if (nw < 0 )
        return error(hin, STATUS_IO_DEVICE_ERROR, 17);
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(17).w16(0).w32(nw).w32(0).w32(0);
      });
      ELLE_LOG("write %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::logoff(SMB2Header* hin)
    {
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(4).w16(0);
      });
      ELLE_LOG("logoff %s", buf.size());
      _socket->write(buf);
    }
    void SMBConnection::notify(SMB2Header* hin)
    {
      elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(9).w16(0).w16(0).w16(0).w8(0x21);
      });
      ELLE_LOG("notify tadaam %s", buf.size());
      SMB2Header* hout = (SMB2Header*)(buf.mutable_contents()+4);
      hout->ntstatus = STATUS_PENDING;
      hout->flags |= 0x2; // async
      hout->reserved1 = hin->messageId;
      _socket->write(buf);
    }
    void SMBConnection::set_info(SMB2Header* hin)
    {
      uint8_t infotype, infoclass;
      uint64_t guid;
      uint32_t bufferLength;
      uint16_t bufferOffset;
      Reader(*hin).skip(2).r8(infotype).r8(infoclass).r32(bufferLength)
      .r16(bufferOffset).skip(2).skip(4).skip(8).r64(guid);
      Reader payload((const char*)hin + bufferOffset);
      bool handled = false;
      if (infotype == 0x01)
      { // FILE
        if (infoclass == FileEndOfFileInformation)
        {
          uint64_t eof;
          payload.r64(eof);
          _file_handles.at(guid).file->truncate(eof);
          handled = true;
        }
        if (infoclass == 13) // fileDispositionInformation
        {
          uint8_t doc;
          payload.r8(doc);
          if (guid >= _directory_start)
          {
            auto& di = _dir_handles.at(guid);
            if (doc)
            {
              bool empty = true;
              di.directory->list_directory([&](std::string const&, struct stat*)
                {
                  empty = false;
                });
              if (!empty)
                return error(hin, STATUS_DIRECTORY_NOT_EMPTY, 2);
            }
            di.deleteOnClose = doc;
          }
          else
            _file_handles.at(guid).deleteOnClose = doc;
          handled = true;
        }
        if (infoclass == FileRenameInformation)
        {
          uint8_t replace;
          uint32_t path_length;
          payload.r8(replace).skip(15).r32(path_length);
          std::string path = from_utf16((const char*)payload._d, path_length);
          std::replace( path.begin(), path.end(), '\\', '/');
          if (guid >= _directory_start)
          {
            _dir_handles.at(guid).directory->rename(path);
          }
          else
          {
            _file_handles.at(guid).file->rename(path);
          }
          ELLE_LOG("rename to %s", path);
          handled = true;
        }
      }
      if (!handled)
        error(hin, STATUS_NOT_IMPLEMENTED, 2);
      else
      {
        elle::Buffer buf = make_reply(*hin, [&](Writer& w) {
          w.w16(2);
        });
        ELLE_LOG("setinfo %s", buf.size());
        _socket->write(buf);
      }
    }
  }
}
