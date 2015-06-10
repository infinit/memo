#ifndef INFINIT_RPC_HXX
# define INFINIT_RPC_HXX

namespace infinit
{
  template <typename ... Args>
  void
  RPC<void (Args...)>::operator ()(Args const& ... args)
  {
    RPCCall<void (Args...)>::_call(*this, args...);
  }

  template <typename R, typename ... Args>
  R
  RPC<R (Args...)>::operator ()(Args const& ... args)
  {
    return RPCCall<R (Args...)>::_call(*this, args...);
  }
}

#endif
