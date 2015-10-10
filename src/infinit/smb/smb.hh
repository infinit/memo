#ifndef INFINIT_SMB_HH
# define INFINIT_SMB_HH
# include <reactor/network/tcp-server.hh>
# include <reactor/filesystem.hh>
# include <infinit/filesystem/filesystem.hh>

namespace infinit
{
  namespace smb
  {
    class SMBConnection;
    class SMBServer
    {
    public:
      SMBServer(std::unique_ptr<infinit::filesystem::FileSystem> fs);
      void _serve();
      std::unique_ptr<reactor::Thread> _server_thread;
      std::unique_ptr<reactor::network::TCPServer> _server;
      std::set<std::unique_ptr<SMBConnection>> _connections;
      std::unique_ptr<reactor::filesystem::FileSystem> _fs;
    };
    
  }
}
#endif