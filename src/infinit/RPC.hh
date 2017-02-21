#pragma once

#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/log.hh>
#include <elle/bench.hh>

#include <cryptography/SecretKey.hh>

#include <reactor/network/exception.hh>
#include <reactor/network/socket.hh>
#include <reactor/storage.hh>

#include <protocol/ChanneledStream.hh>
#include <protocol/Serializer.hh>

#include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  class UnknownRPC
    : public elle::Error
  {
  public:
    UnknownRPC(std::string name)
      : elle::Error(elle::sprintf("unknown RPC: %s", name))
      , _name(std::move(name))
    {}

    UnknownRPC(elle::serialization::SerializerIn& s)
      : elle::Error(s)
      , _name()
    {
      this->_serialize(s);
    }

    void
    serialize(elle::serialization::Serializer& s,
              elle::Version const& version) override
    {
      this->elle::Error::serialize(s, version);
      this->_serialize(s);
    }

    void
    _serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->_name);
    }

    ELLE_ATTRIBUTE_R(std::string, name);
  };

  /*-------.
  | Server |
  `-------*/

  class RPCHandler
  {
  public:
    RPCHandler(std::string name)
      : _name(std::move(name))
    {}
    virtual
    ~RPCHandler() = default;
    virtual
    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) = 0;
    ELLE_ATTRIBUTE_R(std::string, name);
  };

  std::ostream&
  operator <<(std::ostream& output, RPCHandler const& rpc);

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
    using Head = Head_;
    using Tail = List<Tail_...>;
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
    ConcreteRPCHandler(std::string name,
                       std::function<R (Args...)> const& function)
      : RPCHandler(std::move(name))
      , _function(function)
    {}
    ELLE_ATTRIBUTE_R(std::function<R (Args...)>, function);


    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) override
    {
      this->_handle<List<Args...>>(0, input, output);
    }

  private:
    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      !Remaining::empty &&
      !std::is_base_of<
        elle::serialization::VirtuallySerializableBase,
        std::remove_reference_t<typename Remaining::Head>>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      using Head = std::remove_const_t<std::remove_reference_t<typename Remaining::Head>>;
      ELLE_LOG_COMPONENT("infinit.RPC");
      auto arg = input.deserialize<Head>(elle::sprintf("arg%s", n));
      ELLE_DUMP("got argument: %s", arg);
      this->_handle<typename Remaining::Tail,
                    Parsed..., typename Remaining::Head>(
        n + 1, input, output, std::forward<Parsed>(parsed)..., std::move(arg));
    }

    template <typename Remaining, typename ... Parsed>
    typename std::enable_if<
      !Remaining::empty &&
      std::is_base_of<
        elle::serialization::VirtuallySerializableBase,
        std::remove_reference_t<typename Remaining::Head>>::value,
      void>::type
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      using Head = std::remove_const_t<std::remove_reference_t<typename Remaining::Head>>;
      ELLE_LOG_COMPONENT("infinit.RPC");
      auto arg =
        input.deserialize<std::unique_ptr<Head>>(elle::sprintf("arg%s", n));
      ELLE_DUMP("got argument: %s", *arg);
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

    RPCServer();
    RPCServer(elle::Version version);

    ~RPCServer()
    {
      _destroying(this);
    }

    template <typename R, typename ... Args>
    void
    add(std::string const& name, std::function<R (Args...)> f)
    {
      this->_rpcs[name] =
        std::make_unique<ConcreteRPCHandler<R, Args...>>(name, f);
    }

    template <typename Fun>
    void
    add(std::string const& name, Fun fun)
    {
      add(name, std::function<std::get_signature<Fun>>(fun));
    }

    template <typename F, typename ... Args>
    static
    void
    umbrella(F const& f, Args&& ... args)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        f(std::forward<Args>(args)...);
      }
      catch (infinit::protocol::Serializer::EOF const&)
      {}
      catch (reactor::network::ConnectionClosed const& e)
      {}
      catch (reactor::network::SocketClosed const& e)
      {
        ELLE_TRACE("unexpected SocketClosed: %s", e.backtrace());
      }
    }

    void
    serve(std::iostream& s)
    {
      umbrella(
        [&]
        {
          protocol::Serializer serializer(
            s, elle_serialization_version(this->_version), false);
          this->_serve(serializer);
        });
    }

    void
    serve(protocol::Serializer& serializer)
    {
      umbrella([&] { this->_serve(serializer); });
    }

    void
    _serve(protocol::Serializer& serializer)
    {
      auto chans = protocol::ChanneledStream{serializer};
      this->_serve(chans);
    }

    void
    serve(protocol::ChanneledStream& channels)
    {
      umbrella([&] { this->_serve(channels); });
    }

    void
    _serve(protocol::ChanneledStream& channels)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      while (true)
      {
	auto channel = channels.accept();
	auto request = channel.read();
	ELLE_TRACE_SCOPE("%s: process RPC", this);
	bool had_key = !!_key;
	if (had_key)
	{
          ELLE_DEBUG_SCOPE("decipher RPC");
	  try
	  {
	    static elle::Bench bench("bench.rpcserve.decipher", 10000_sec);
	    elle::Bench::BenchScope bs(bench);
	    if (request.size() > 262144)
	    {
	      auto& key = this->_key.get();
	      elle::With<reactor::Thread::NonInterruptible>() << [&] {
	        reactor::background([&] {
	            request = key.decipher(request);
	        });
	      };
	    }
	    else
	      request = this->_key->decipher(request);
	  }
	  catch(std::exception const& e)
	  {
	    ELLE_ERR("decypher request: %s", e.what());
	    throw;
	  }
	}
	elle::IOStream ins(request.istreambuf());
	auto versions = elle::serialization::get_serialization_versions
	  <infinit::serialization_tag>(this->_version);
	elle::serialization::binary::SerializerIn input(ins, versions, false);
	input.set_context(this->_context);
	std::string name;
	input.serialize("procedure", name);
        elle::Buffer response;
        {
          auto it = this->_rpcs.find(name);
          elle::IOStream outs(response.ostreambuf());
          elle::serialization::binary::SerializerOut output(
            outs, versions, false);
          if (it == this->_rpcs.end())
          {
            ELLE_WARN("%s: unknown RPC: %s", *this, name);
            output.serialize("success", false);
            output.serialize(
              "exception", std::make_exception_ptr<UnknownRPC>(name));
          }
          else
          {
            ELLE_TRACE_SCOPE("%s: run procedure %s", *this, name);
            {
              output.set_context(this->_context);
              try
              {
                it->second->handle(input, output);
              }
              catch (elle::Error const& e)
              {
                ELLE_WARN("%s: deserialization error: %s",
                          *this, e);
                throw;
              }
            }
          }
        }
	if (had_key)
	{
	  static elle::Bench bench("bench.rpcserve.encipher", 10000_sec);
	  elle::Bench::BenchScope bs(bench);
	  if (response.size() >= 262144)
	  {
	    auto& key = this->_key.get();
	    elle::With<reactor::Thread::NonInterruptible>() << [&] {
	      reactor::background([&] {
	          response = key.encipher(
	            elle::ConstWeakBuffer(response.contents(),
                                          response.size()));
	      });
	    };
	  }
	  else
	    response = _key->encipher(
	      elle::ConstWeakBuffer(response.contents(), response.size()));
	}
	channel.write(response);
      }
    }

    template <typename T>
    void
    set_context(T value)
    {
      this->_context.set<T>(value);
    }

    std::unordered_map<std::string, std::unique_ptr<RPCHandler>> _rpcs;
    elle::serialization::Context _context;
    boost::optional<infinit::cryptography::SecretKey> _key;
    boost::signals2::signal<void(RPCServer*)> _destroying;
    boost::signals2::signal<void(RPCServer*)> _ready;
    ELLE_ATTRIBUTE(elle::Version, version);
  };

  /*-------.
  | Client |
  `-------*/

  class BaseRPC
  {
  public:
    using Passport = infinit::model::doughnut::Passport;
    BaseRPC(std::string name,
            protocol::ChanneledStream& channels,
            elle::Version const& version,
            boost::optional<cryptography::SecretKey> key = {});

    elle::Buffer
    credentials()
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
                *this, elle::type_info<T>(), value);
      this->_context.template set<T>(value);
    }

    elle::serialization::Context _context;
    ELLE_ATTRIBUTE_R(std::string, name);
    ELLE_ATTRIBUTE_R(protocol::ChanneledStream*, channels, protected);
    ELLE_ATTRIBUTE_RX(
      boost::optional<infinit::cryptography::SecretKey>, key, protected);
    ELLE_ATTRIBUTE_R(elle::Version, version, protected);
  };

  template <typename Proto>
  class RPC
  {};

  template <typename ... Args>
  class RPC<void (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name,
        protocol::ChanneledStream& channels,
        elle::Version const& version,
        boost::optional<cryptography::SecretKey> key = {})
      : BaseRPC(std::move(name), channels, version, std::move(key))
    {}

    RPC(std::string name,
        protocol::ChanneledStream& channels,
        elle::Version const& version,
        elle::Buffer* credentials)
      : RPC(
        std::move(name),
        channels,
        version,
        credentials && !credentials->empty() ?
        boost::optional<cryptography::SecretKey>(elle::Buffer(*credentials)) :
        boost::optional<cryptography::SecretKey>())
    {}

    void
    operator ()(Args const& ... args);
    using result_type = void;
  };

  template <typename R, typename ... Args>
  class RPC<R (Args...)>
    : public BaseRPC
  {
  public:
    RPC(std::string name,
        protocol::ChanneledStream& channels,
        elle::Version const& version,
        boost::optional<cryptography::SecretKey> key = {})
      : BaseRPC(std::move(name), channels, version, std::move(key))
    {}

    RPC(std::string name,
        protocol::ChanneledStream& channels,
        elle::Version const& version,
        elle::Buffer* credentials)
      : RPC(
        std::move(name),
        channels,
        version,
        credentials && !credentials->empty() ?
        boost::optional<cryptography::SecretKey>(elle::Buffer(*credentials)) :
        boost::optional<cryptography::SecretKey>())
    {}

    R
    operator ()(Args const& ... args);
    using result_type = R;
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
    typename std::enable_if<
      std::is_base_of<
        elle::serialization::VirtuallySerializableBase,
        std::remove_cv_reference_t<Head>>::value,
      void>::type
    call_arguments(int n,
                   elle::serialization::SerializerOut& output,
                   Head&& head,
                   Tail&& ... tail)
    {
      using RawHead = std::remove_const_t<std::remove_reference_t<Head>>;
      RawHead* ptr = const_cast<RawHead*>(&head);
      output.serialize(elle::sprintf("arg%s", n), ptr);
      call_arguments(n + 1, output, std::forward<Tail>(tail)...);
    }

    template <typename Head, typename ... Tail>
    static
    typename std::enable_if<
      !std::is_base_of<elle::serialization::VirtuallySerializableBase,
                       std::remove_reference_t<Head>>::value,
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
      return input.deserialize<R>("value");
    }

    static
    typename std::conditional<std::is_same<R, void>::value, int, R>::type
    _call(elle::Version const& version,
          RPC<R (Args...)>& self,
          Args const&... args)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_TRACE_SCOPE("%s: call", self);
      auto versions = elle::serialization::get_serialization_versions
        <infinit::serialization_tag>(version);
      protocol::Channel channel(*self.channels());
      {
        elle::Buffer call;
        elle::IOStream outs(call.ostreambuf());

        ELLE_DEBUG("build request")
        {
          elle::serialization::binary::SerializerOut output(outs, versions, false);
          output.set_context(self._context);
          output.serialize("procedure", self.name());
          call_arguments(0, output, args...);
        }
        outs.flush();
        if (self.key())
        {
          static elle::Bench bench("bench.rpcclient.encipher", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          ELLE_DEBUG("encipher request")
            if (call.size() > 262144)
            {
              elle::With<reactor::Thread::NonInterruptible>() << [&] {
                reactor::background([&] {
                    call = self.key()->encipher(
                      elle::ConstWeakBuffer(call.contents(), call.size()));
                  });
              };
            }
            else
              call = self.key()->encipher(
                elle::ConstWeakBuffer(call.contents(), call.size()));
        }
        ELLE_DEBUG("send request")
          channel.write(call);
      }
      ELLE_DEBUG("read response request")
      {
        auto response = channel.read();
        if (self.key())
        {
          static elle::Bench bench("bench.rpcclient.decipher", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          if (response.size() > 262144)
          {
            elle::With<reactor::Thread::NonInterruptible>() << [&] {
              reactor::background([&] {
                  response = self.key()->decipher(
                    elle::ConstWeakBuffer(response.contents(), response.size()));
              });
            };
          }
          else
            response = self.key()->decipher(
              elle::ConstWeakBuffer(response.contents(), response.size()));
        }
        elle::IOStream ins(response.istreambuf());
        elle::serialization::binary::SerializerIn input(ins, versions, false);
        input.set_context(self._context);
        bool success = false;
        input.serialize("success", success);
        if (success)
        {
          ELLE_TRACE_SCOPE("get result");
          auto res = get_result<R>(input);
          ELLE_DUMP("result: %s", res);
          return std::move(res);
        }
        else
        {
          ELLE_TRACE_SCOPE("call failed, get exception");
          auto e =
            input.deserialize<std::exception_ptr>("exception");
          std::rethrow_exception(e);
        }
      }
    }
  };

  inline
  BaseRPC::BaseRPC(std::string name,
                   protocol::ChanneledStream& channels,
                   elle::Version const& version,
                   boost::optional<cryptography::SecretKey> key)
    : _name(std::move(name))
    , _channels(&channels)
    , _key(std::move(key))
    , _version(version)
  {}

  std::ostream&
  operator <<(std::ostream& o, BaseRPC const& rpc);
}

#include <infinit/RPC.hxx>
