namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template<typename F>
      template<typename ...Args>
      typename RPC<F>::result_type
      RemoteRPC<F>::operator()(Args const& ... args)
      {
        ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote.rpc");
        int attempt = 0;
        bool need_reconnect = false;
        while (true)
        {
          try
          {
            if (need_reconnect)
              _remote->reconnect(15_sec);
            else
              _remote->connect(15_sec);
            this->_channels = _remote->channels().get();
            return RPC<F>::operator()(args...);
          }
          catch(reactor::network::Exception const& e)
          {
            ELLE_TRACE("network exception when invoking %s: %s",
                       this->name(), e);
          }
          if (++attempt >= 10)
          {
            throw elle::Error(elle::sprintf("could not establish channel for RPC '%s'",
                                            this->name()));
          }
          reactor::sleep(boost::posix_time::milliseconds(200 * attempt));
          need_reconnect = true;
        }
      }
    }
  }
}