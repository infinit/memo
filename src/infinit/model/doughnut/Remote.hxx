#include <elle/chrono.hh>
#include <elle/os/environ.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template<typename F, typename ...Args>
      typename RPC<F>::result_type
      remote_call_next(RemoteRPC<F>* ptr, Args const& ... args)
      {
        return ptr->RPC<F>::operator()(args...);
      }

      template<typename F>
      template<typename ...Args>
      typename RPC<F>::result_type
      RemoteRPC<F>::operator()(Args const& ... args)
      {
        // GCC bug, argument packs dont work in lambdas
        auto helper = std::bind(&remote_call_next<F, Args...>,
          this, std::ref(args)...);
        return _remote->safe_perform<typename RPC<F>::result_type>(
          this->name(),
          [&]
          {
            ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote");
            this->_channels = _remote->_connection->channels().get();
            auto creds = _remote->credentials();
            if (!creds.empty())
            {
              elle::Buffer c(creds);
              this->key().emplace(std::move(c));
            }
            return helper();
        });
      }

      template<typename R>
      R
      Remote::safe_perform(std::string const& name,
                           std::function<R()> op)
      {
        ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote");
        static auto const rpc_timeout =
          elle::chrono::duration_parse<std::milli>(
            elle::os::getenv("INFINIT_CONNECT_TIMEOUT", "5"));
        static auto const soft_fail =
          elle::chrono::duration_parse<std::milli>(
            elle::os::getenv("INFINIT_SOFTFAIL_TIMEOUT", "20"));
        auto const rpc_start = std::chrono::system_clock::now();
        auto disconnected_for = std::chrono::system_clock::now() -
          this->_connection->disconnected_since();
        // No matter what, if we are disconnected, retry.
        if (this->_connection->disconnected())
        {
          ELLE_TRACE("%s: reconnect before running \"%s\"", this, name);
          this->reconnect();
        }
        // If we exceeded the connection time, retry.
        else if (!this->_connection->connected() &&
                 disconnected_for >= rpc_timeout)
        {
          ELLE_TRACE("%s: drop stale connection before running \"%s\"",
                     this, name);
          this->reconnect();
        }
        bool give_up = false;
        while (true)
        {
          auto const rpc_timeout_delay =
            rpc_timeout - (std::chrono::system_clock::now() - rpc_start);
          auto const disconnected_for =
            std::chrono::system_clock::now() - this->_connecting_since;
          auto const soft_fail_delay = soft_fail - disconnected_for;
          try
          {
            // Try connecting until we reach the RPC timeout or the Remote
            // softfail.
            reactor::Duration const delay =
              boost::posix_time::millisec(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::min(rpc_timeout_delay, soft_fail_delay)).count());
            if (reactor::wait(this->_connected, delay))
            {
              ELLE_TRACE("%s: run \"%s\"", this, name);
              return op();
            }
            else
            {
              ELLE_TRACE("%s: soft-fail running \"%s\"", this, name);
              give_up = true;
            }
          }
          catch(reactor::network::Exception const& e)
          {
            ELLE_TRACE("%s: network exception when invoking %s: %s",
                       this, name, e);
          }
          catch(infinit::protocol::Serializer::EOF const& e)
          {
            ELLE_TRACE("%s: EOF when invoking %s: %s", this, name, e);
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("%s: connection error: %s", this, e);
            throw;
          }
          if (give_up)
            if (rpc_timeout_delay < soft_fail_delay)
            {
              ELLE_TRACE("%s: give up rpc %s after %s",
                         this, name, rpc_timeout);
              throw reactor::network::TimeOut();
            }
            else
            {
              ELLE_TRACE(
                "%s: soft-fail rpc %s after remote was disconnected for %s",
                this, name, disconnected_for);
              ELLE_ASSERT(this->_disconnected_exception);
              std::rethrow_exception(this->_disconnected_exception);
            }
          else
            this->reconnect();
        }
      }

      template <typename F>
      RemoteRPC<F>::RemoteRPC(std::string name, Remote* remote)
        : Super(name,
                *remote->_connection->channels(),
                remote->doughnut().version(),
                elle::unconst(&remote->credentials()))
        , _remote(remote)
      {
        this->set_context(remote);
      }

      template<typename F>
      RemoteRPC<F>
      Remote::make_rpc(std::string const& name)
      {
        return RemoteRPC<F>(name, this);
      }
    }
  }
}
