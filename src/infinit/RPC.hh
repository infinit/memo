#ifndef INFINIT_RPC_HH
# define INFINIT_RPC_HH

# include <elle/serialization/json.hh>
# include <elle/serialization/binary.hh>
# include <elle/log.hh>

# include <cryptography/SecretKey.hh>

# include <reactor/network/exception.hh>
# include <reactor/network/socket.hh>
# include <reactor/storage.hh>

# include <protocol/ChanneledStream.hh>
# include <protocol/Serializer.hh>

# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  /*-------.
  | Server |
  `-------*/

  class RPCHandler
  {
  public:
    virtual
    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) = 0;
  };

#ifdef __clang__
  // Clang fails on the other simpler list implementation by
  // matching List<Foo> to the default impl on some conditions.
  template<int I, typename ... Args> struct ListImpl {};

  template<typename ... Args>
  struct List
   : public ListImpl<sizeof...(Args), Args...>
  {
  };

  template <typename ... Args>
  struct ListImpl<0, Args...>
  {
    static constexpr bool empty = true;
    static constexpr int nargs = sizeof...(Args);
  };

  template <int I, typename Head_, typename ... Tail_>
  struct ListImpl<I, Head_, Tail_ ...>
  {
    typedef Head_ Head;
    typedef List<Tail_...> Tail;
    static constexpr bool empty = false;
  };

#else

  template <typename ... Args>
  struct List
  {
    static constexpr bool empty = true;
  };

  template <typename Head_, typename ... Tail_>
  struct List<Head_, Tail_...>
  {
    typedef Head_ Head;
    typedef List<Tail_...> Tail;
    static constexpr bool empty = false;
  };

#endif

  template <typename R, typename ... Args>
  class
  ConcreteRPCHandler
    : public RPCHandler
  {
  public:
    ConcreteRPCHandler(std::function<R (Args...)> const& function)
      : _function(function)
    {}
    ELLE_ATTRIBUTE_R(std::function<R (Args...)>, function);

    virtual
    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) override
    {
      this->_handle<List<Args...>>(0, input, output);
    }

  private:
    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<!Remaining::empty && !std::is_base_of<elle::serialization::VirtuallySerializable, typename std::remove_reference<typename Remaining::Head>::type>::value, void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      typedef
        typename std::remove_const<
          typename std::remove_reference<typename Remaining::Head>::type>::type
        Head;
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_DEBUG("%s: get argument %s", *this, n);
      auto arg = input.deserialize<Head>(elle::sprintf("arg%s", n));
      ELLE_DEBUG("%s: got argument: %s", *this, arg);
      this->_handle<typename Remaining::Tail,
                    Parsed..., typename Remaining::Head>(
        n + 1, input, output, std::forward<Parsed>(parsed)..., std::move(arg));
    }

    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      !Remaining::empty &&
      std::is_base_of<elle::serialization::VirtuallySerializable,
                      typename std::remove_reference<typename Remaining::Head>::type>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      typedef
        typename std::remove_const<typename std::remove_reference<typename Remaining::Head>::type>::type
        Head;
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_DEBUG("%s: get argument %s", *this, n);
      auto arg =
        input.deserialize<std::unique_ptr<Head>>(elle::sprintf("arg%s", n));
      ELLE_DEBUG("%s: got argument: %s", *this, *arg);
      this->_handle<typename Remaining::Tail,
                    Parsed..., typename Remaining::Head>(
        n + 1, input, output, std::forward<Parsed>(parsed)..., std::move(*arg));
    }
    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      Remaining::empty && std::is_same<R, void>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        ELLE_TRACE_SCOPE("%s: run", *this);
        this->_function(std::forward<Parsed>(parsed)...);
        ELLE_TRACE("%s: success", *this);
        output.serialize("success", true);
      }
      catch (elle::Error& e)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
        output.serialize("exception", std::current_exception());
      }
      catch (...)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
      }
    }

    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      Remaining::empty && !std::is_same<R, void>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        ELLE_TRACE_SCOPE("%s: run", *this);
        R res = this->_function(std::forward<Parsed>(parsed)...);
        ELLE_TRACE("%s: success: %s", *this, res);
        output.serialize("success", true);
        output.serialize("value", res);
      }
      catch (elle::Error& e)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
        output.serialize("exception", std::current_exception());
      }
      catch (...)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
      }
    }
  };

  class RPCServer
  {
  public:
    using Passport = infinit::model::doughnut::Passport;
    using Doughnut = infinit::model::doughnut::Doughnut;
    template <typename R, typename ... Args>
    void
    add(std::string const& name, std::function<R (Args...)> f)
    {
      this->_rpcs[name] =
        elle::make_unique<ConcreteRPCHandler<R, Args...>>(f);
    }

    RPCServer(Doughnut* doughnut = nullptr)
      : _doughnut(doughnut)
    {
    }
    void
    serve(std::iostream& s)
    {
      protocol::Serializer serializer(s, false);
      serve(serializer);
    }

    void
    serve(protocol::Serializer& serializer)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        protocol::ChanneledStream channels(serializer);
        while (true)
        {
          auto channel = channels.accept();
          auto request = channel.read();
          ELLE_DEBUG("Processing one request, key=%s, len=%s data=%x",
            !!this->_key.Get(), request.size(), request);
          bool had_key = !!_key.Get();
          if (had_key)
          {
            try
            {
              request = this->_key.Get()->decipher(request);
              ELLE_DEBUG("Wrote %s plain bytes", request.size());
            }
            catch(std::exception const& e)
            {
              ELLE_ERR("decypher request: %s", e.what());
              throw;
            }
          }
          ELLE_DEBUG("Deserializing...");
          elle::IOStream ins(request.istreambuf());
          elle::serialization::binary::SerializerIn input(ins, false);
          input.set_context(this->_context);
          std::string name;
          input.serialize("procedure", name);
          auto it = this->_rpcs.find(name);
          if (it == this->_rpcs.end())
          {
            ELLE_WARN("%s: unknown RPC: %s", *this, name);
            throw elle::Error(elle::sprintf("unknown RPC: %s", name));
          }
          ELLE_TRACE_SCOPE("%s: run procedure %s", *this, name);
          elle::Buffer response;
          elle::IOStream outs(response.ostreambuf());
          {
            elle::serialization::binary::SerializerOut output(outs, false);
            try
            {
              it->second->handle(input, output);
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("%s: deserialization error: %s",
                        *this, elle::exception_string());
              throw;
            }
          }

          outs.flush();
          if (had_key)
          {
            response = _key.Get()->encipher(
              elle::ConstWeakBuffer(response.contents(), response.size()));
          }
          channel.write(response);
        }
      }
      catch (reactor::network::ConnectionClosed const&)
      {}
      catch (reactor::network::SocketClosed const&)
      {}
    }

    template <typename T>
    void
    set_context(T value)
    {
      this->_context.set<T>(value);
    }

    std::unordered_map<std::string, std::unique_ptr<RPCHandler>> _rpcs;
    elle::serialization::Context _context;
    reactor::LocalStorage<std::unique_ptr<infinit::cryptography::SecretKey>> _key;
    infinit::model::doughnut::Doughnut* _doughnut;

  };

  /*-------.
  | Client |
  `-------*/

  class BaseRPC
  {
  public:
    using Passport = infinit::model::doughnut::Passport;
    using Doughnut = infinit::model::doughnut::Doughnut;
    BaseRPC(std::string name, protocol::ChanneledStream& channels,
            elle::Buffer* credentials = nullptr);

    elle::Buffer credentials()
    {
      if (this->_key)
        return this->_key->password();
      else
        return {};
    }
    template <typename T>
    void
    set_context(T value)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_DUMP("%s: set context for %s: %s",
                *this, elle::demangle(typeid(T).name()), value);
      this->_context.template set<T>(value);
    }

    elle::serialization::Context _context;
    ELLE_ATTRIBUTE_R(std::string, name);
    ELLE_ATTRIBUTE_R(protocol::ChanneledStream*, channels, protected);
    ELLE_ATTRIBUTE_RX(std::unique_ptr<infinit::cryptography::SecretKey>, key, protected);
  };

  template <typename Proto>
  class RPC
  {};

  template <typename ... Args>
  class RPC<void (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name, protocol::ChanneledStream& channels,
      elle::Buffer* credentials = nullptr)
      : BaseRPC(std::move(name), channels, credentials)
    {}

    void
    operator ()(Args const& ... args);
    typedef void result_type;
  };

  template <typename R, typename ... Args>
  class RPC<R (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name, protocol::ChanneledStream& channels,
        elle::Buffer* credentials = nullptr)
      : BaseRPC(std::move(name), channels, credentials)
    {}

    R
    operator ()(Args const& ... args);
    typedef R result_type;
  };

  template <typename T>
  struct
  RPCCall
  {};

  template <typename R, typename ... Args>
  struct
  RPCCall<R (Args...)>
  {
    template <typename Head, typename ... Tail>
    static
    typename std::enable_if<std::is_base_of<elle::serialization::VirtuallySerializable,
                      typename std::remove_const<typename std::remove_reference<Head>::type>::type>::value,
                      void>::type
    call_arguments(int n,
                   elle::serialization::SerializerOut& output,
                   Head&& head,
                   Tail&& ... tail)
    {
      typedef
      typename std::remove_const<typename std::remove_reference<Head>::type>::type
        RawHead;
      RawHead* ptr = const_cast<RawHead*>(&head);
      output.serialize(elle::sprintf("arg%s", n), ptr);
      call_arguments(n + 1, output, std::forward<Tail>(tail)...);
    }

    template <typename Head, typename ... Tail>
    static
    typename std::enable_if<!std::is_base_of<elle::serialization::VirtuallySerializable,
      typename std::remove_reference<Head>::type>::value,
    void>::type
    call_arguments(int n,
                   elle::serialization::SerializerOut& output,
                   Head&& head,
                   Tail&& ... tail)
    {
      output.serialize(elle::sprintf("arg%s", n), head);
      call_arguments(n + 1, output, std::forward<Tail>(tail)...);
    }

    static
    void
    call_arguments(int, elle::serialization::SerializerOut&)
    {}

    template <typename Res>
    static
    typename std::enable_if<std::is_same<Res, void>::value, int>::type
    get_result(elle::serialization::SerializerIn&)
    {
      return 0;
    }

    template <typename Res>
    static
    typename std::enable_if<!std::is_same<Res, void>::value, R>::type
    get_result(elle::serialization::SerializerIn& input)
    {
      R result;
      input.serialize("value", result);
      return std::move(result);
    }

    static
    typename std::conditional<std::is_same<R, void>::value, int, R>::type
    _call(RPC<R (Args...)>& self, Args const&... args)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_TRACE_SCOPE("%s: call", self);
      protocol::Channel channel(*self.channels());
      {
        elle::Buffer call;
        elle::IOStream outs(call.ostreambuf());
        ELLE_DEBUG("%s: build request", self)
        {
          elle::serialization::binary::SerializerOut output(outs, false);
          output.serialize("procedure", self.name());
          call_arguments(0, output, args...);
        }
        outs.flush();
        if (self.key())
        {
          call = self.key()->encipher(
            elle::ConstWeakBuffer(call.contents(), call.size()));
        }
        ELLE_DEBUG("%s: send request", self)
          channel.write(call);
      }
      {
        ELLE_DEBUG_SCOPE("%s: read response request", self);
        auto response = channel.read();
        if (self.key())
        {
          response = self.key()->decipher(
            elle::ConstWeakBuffer(response.contents(), response.size()));
        }
        elle::IOStream ins(response.istreambuf());
        elle::serialization::binary::SerializerIn input(ins, false);
        input.set_context(self._context);
        bool success = false;
        input.serialize("success", success);
        if (success)
        {
          ELLE_TRACE_SCOPE("%s: get result", self);
          auto res = get_result<R>(input);
          // not everything is printable
          // ELLE_DUMP("%s: result: %s", self, res);
          return std::move(res);
        }
        else
        {
          ELLE_TRACE_SCOPE("%s: call failed, get exception", self);
          auto e =
            input.deserialize<std::exception_ptr>("exception");
          std::rethrow_exception(e);
        }
      }
    }
  };

  inline BaseRPC::BaseRPC(std::string name,
                          protocol::ChanneledStream& channels,
                          elle::Buffer* credentials
    )
    : _name(std::move(name))
    , _channels(&channels)
    {
      if (credentials && !credentials->empty())
      {
        elle::Buffer creds(*credentials);
        _key = elle::make_unique<cryptography::SecretKey>(std::move(creds));
      }
    }
}



# include <infinit/RPC.hxx>

#endif
