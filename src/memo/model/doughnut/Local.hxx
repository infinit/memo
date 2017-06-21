#include <elle/reactor/for-each.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      template <typename R, typename ... Args>
      R
      Local::broadcast(std::string const& name, Args&& ... args)
      {
        ELLE_LOG_COMPONENT("memo.model.doughnut.Local");
        ELLE_TRACE_SCOPE("%s: broadcast %s to %s peers",
                         this, name, this->_peers.size());
        using Rpc = RPC<auto (Args const& ...) -> R>;
        // Copy peers to hold connections refcount, as for_each_parallel
        // captures values by ref.
        auto peers = this->_peers;
        auto clear = [&]
          {
            // Delay termination from destructor.
            elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
            {
              peers.clear();
            };
          };
        try
        {
          elle::reactor::for_each_parallel(
            peers,
            [&] (std::shared_ptr<Connection> const& c)
            {
              // Arguments taken by reference as they will be passed multiple
              // times.
              auto rpc = Rpc(name, c->_channels,
                             this->version(), c->_rpcs._key);
              // Workaround GCC 4.9 ICE: argument packs don't work through
              // lambdas.
              auto const f = std::bind(&Rpc::operator (),
                                       &rpc, std::ref(args)...);
              try
              {
                RPCServer::umbrella(f);
              }
              catch (UnknownRPC const& e)
              {
                // FIXME: Ignore? Evict? Should probably be configurable. So
                // far only Kouncil uses this, and it's definitely an ignore.
                ELLE_WARN("error contacting %s: %s", c, e);
              }
            },
            elle::sprintf("%s: broadcast RPC %s", this, name));
        }
        catch (...)
        {
          clear();
          throw;
        }
        clear();
      }
    }
  }
}
