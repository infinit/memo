#ifndef INFINIT_RPC_HH
# define INFINIT_RPC_HH

# include <elle/serialization/json.hh>
# include <elle/log.hh>

# include <protocol/ChanneledStream.hh>
# include <protocol/Serializer.hh>

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
    typename std::enable_if<!Remaining::empty, void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed& ... parsed)
    {
      typedef
        typename std::remove_reference<typename Remaining::Head>::type
        Head;
      auto arg = input.deserialize<Head>(elle::sprintf("arg%s", n));
      this->_handle<typename Remaining::Tail, Parsed..., Head&>(
        n + 1, input, output, parsed..., arg);
    }

    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      Remaining::empty && std::is_same<R, void>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed& ... parsed)
    {
      try
      {
        this->_function(parsed...);
        output.serialize("success", true);
      }
      catch (...)
      {
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
            Parsed& ... parsed)
    {
      try
      {
        R res = this->_function(parsed...);
        output.serialize("success", true);
        output.serialize("value", res);
      }
      catch (...)
      {
        output.serialize("success", false);
      }
    }
  };

  class RPCServer
  {
  public:
    template <typename R, typename ... Args>
    void
    add(std::string const& name, std::function<R (Args...)> f)
    {
      this->_rpcs[name] =
        elle::make_unique<ConcreteRPCHandler<R, Args...>>(f);
    }

    void
    serve(reactor::network::Socket& s)
    {
      protocol::Serializer serializer(s);
      protocol::ChanneledStream channels(serializer);
      while (true)
      {
        auto channel = channels.accept();
        auto request = channel.read();
        elle::serialization::json::SerializerIn input(request);
        std::string name;
        input.serialize("procedure", name);
        auto it = this->_rpcs.find(name);
        ELLE_ASSERT(it != this->_rpcs.end());
        protocol::Packet response;
        {
          elle::serialization::json::SerializerOut output(response);
          it->second->handle(input, output);
        }
        channel.write(response);
      }
    }

    std::unordered_map<std::string, std::unique_ptr<RPCHandler>> _rpcs;
  };

  /*-------.
  | Client |
  `-------*/

  class BaseRPC
  {
  public:
    BaseRPC(std::string name, protocol::ChanneledStream& channels)
      : _name(std::move(name))
      , _channels(channels)
    {}

    ELLE_ATTRIBUTE_R(std::string, name);
    ELLE_ATTRIBUTE_R(protocol::ChanneledStream&, channels);
  };

  template <typename Proto>
  class RPC
  {};

  template <typename ... Args>
  class RPC<void (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name, protocol::ChanneledStream& channels)
      : BaseRPC(std::move(name), channels)
    {}

    void
    operator ()(Args const& ... args);
  };

  template <typename R, typename ... Args>
  class RPC<R (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name, protocol::ChanneledStream& channels)
      : BaseRPC(std::move(name), channels)
    {}

    R
    operator ()(Args const& ... args);
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
    void
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
      protocol::Channel channel(self.channels());
      {
        protocol::Packet call;
        ELLE_DEBUG("%s: build request", self)
        {
          elle::serialization::json::SerializerOut output(call);
          output.serialize("procedure", self.name());
          call_arguments(0, output, args...);
        }
        ELLE_DEBUG("%s: send request", self)
          channel.write(call);
      }
      {
        ELLE_DEBUG_SCOPE("%s: read response request", self);
        auto response = channel.read();
        elle::serialization::json::SerializerIn input(response);
        bool success = false;
        input.serialize("success", success);
        ELLE_ASSERT(success);
        return get_result<R>(input);
      }
    }
  };
}

# include <infinit/RPC.hxx>

#endif
