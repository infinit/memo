#include <infinit/model/MonitoringServer.hh>

#include <sstream>

#include <boost/filesystem.hpp>

#include <elle/serialization/json.hh>

#include <reactor/network/exception.hh>
#include <reactor/Scope.hh>

#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.MonitoringServer");

namespace infinit
{
  namespace model
  {
    /*--------------.
    | Monitor Query |
    `--------------*/

    static
    std::string
    query_str(MonitoringServer::MonitorQuery::Query query)
    {
      using Query = MonitoringServer::MonitorQuery::Query;
      switch (query)
      {
        case Query::Consensus:
          return "consensus";
        case Query::Overlay:
          return "overlay";
        case Query::Peers:
          return "peers";
        case Query::Redundancy:
          return "redundancy";
        case Query::Status:
          return "status";
      }
    }

    static
    MonitoringServer::MonitorQuery::Query
    query_val(std::string const& query_str)
    {
      using Query = MonitoringServer::MonitorQuery::Query;
      if (query_str == "consensus")
        return Query::Consensus;
      else if (query_str == "overlay")
        return Query::Overlay;
      else if (query_str == "peers")
        return Query::Peers;
      else if (query_str == "redundancy")
        return Query::Redundancy;
      else if (query_str == "status")
        return Query::Status;
      else
        elle::err("unknown query: %s", query_str);
    }

    MonitoringServer::MonitorQuery::MonitorQuery(
      MonitoringServer::MonitorQuery::Query query)
      : query(query)
    {}

    MonitoringServer::MonitorQuery::MonitorQuery(
      elle::serialization::SerializerIn& s)
      : query(query_val(s.deserialize<std::string>("query")))
    {}

    void
    MonitoringServer::MonitorQuery::serialize(
      elle::serialization::Serializer& s)
    {
      std::string temp = s.out() ? query_str(this->query) : "";
      s.serialize("query", temp);
      this->query = query_val(temp);
    }

    void
    MonitoringServer::MonitorQuery::print(std::ostream& stream) const
    {
      stream << "MonitorQuery(" << query_str(this->query) << ")";
    }

    /*-----------------.
    | Monitor Response |
    `-----------------*/

    MonitoringServer::MonitorResponse::MonitorResponse(
      bool success,
      boost::optional<std::string> error)
      : success(success)
      , error(std::move(error))
    {}

    MonitoringServer::MonitorResponse::MonitorResponse(
      elle::serialization::SerializerIn& s)
      : success(s.deserialize<bool>("success"))
      , error(s.deserialize<boost::optional<std::string>>("error"))
    {}

    void
    MonitoringServer::MonitorResponse::serialize(
      elle::serialization::Serializer& s)
    {
      s.serialize("success", this->success);
      s.serialize("error", this->error);
    }

    void
    MonitoringServer::MonitorResponse::print(std::ostream& stream) const
    {
      stream << "MonitorResponse(" << (this->success ? "success" : "fail");
      if (this->error)
        stream << ", error: " << this->error.get();
      stream << ")";
    }

    void
    MonitoringServer::MonitorResponse::pretty_print(std::ostream& stream) const
    {
      if (this->error)
        stream << "Error: " << this->error << std::endl;
    }

    /*-------------------------.
    | Monitor Response Generic |
    `-------------------------*/

    MonitoringServer::MonitorResponseGeneric::MonitorResponseGeneric(
      elle::json::Json result)
      : MonitoringServer::MonitorResponse(true)
      , result(std::move(result))
    {}

    MonitoringServer::MonitorResponseGeneric::MonitorResponseGeneric(
      elle::serialization::SerializerIn& s)
      : MonitoringServer::MonitorResponse(s)
    {
      std::string temp;
      temp = s.deserialize<std::string>("result");
      std::stringstream ss(temp);
      {
        this->result = elle::json::read(ss);
      }
    }

    void
    MonitoringServer::MonitorResponseGeneric::serialize(
      elle::serialization::Serializer& s)
    {
      MonitoringServer::MonitorResponse::serialize(s);
      if (s.in())
      {
        std::string temp;
        s.serialize("result", temp);
        std::stringstream ss(temp);
        {
          this->result = elle::json::read(ss);
        }
      }
      else
      {
        std::stringstream ss;
        elle::json::write(ss, this->result);
        std::string temp = ss.str();
        s.serialize("result", temp);
      }
    }

    static const elle::serialization::Hierarchy<
      MonitoringServer::MonitorResponse>::Register<
        MonitoringServer::MonitorResponseGeneric>
          _register_MonitorResponseGeneric("generic");

    static
    std::string
    convert_any(boost::any const& any, int indent = 0, bool first = false)
    {
      static int indent_size = 2;
      if (any.type() == typeid(elle::json::OrderedObject))
      {
        indent += 1;
        std::string res;
        for (auto const& elem: boost::any_cast<elle::json::OrderedObject>(any))
        {
          res += elle::sprintf(
            "%s%s%s: %s",
            (first ? "" : "\n"), std::string(indent * indent_size, ' '),
            elem.first, convert_any(elem.second, indent));
          first = false;
        }
        return res;
      }
      if (any.type() == typeid(elle::json::Object))
      {
        indent += 1;
        std::string res;
        for (auto const& elem: boost::any_cast<elle::json::Object>(any))
        {
          res += elle::sprintf(
            "%s%s%s: %s",
            (first ? "" : "\n"), std::string(indent * indent_size, ' '),
            elem.first, convert_any(elem.second, indent));
          first = false;
        }
        return res;
      }
      if (any.type() == typeid(elle::json::Array))
      {
        std::string res;
        for (auto const& elem: boost::any_cast<elle::json::Array>(any))
        {
          res += elle::sprintf(
            "%s%s%s",
            (first ? "" : "\n"), std::string(indent * indent_size, ' '),
            convert_any(elem, indent));
          first = false;
        }
        return res;
      }
#ifdef __clang__
# define CL(a) std::string((a).name())
#else
# define CL(a) (a)
#endif
#define RET(a) return elle::sprintf("%s", a)
      if (CL(any.type()) == CL(typeid(std::string)))
        RET(boost::any_cast<std::string>(any));
      if (CL(any.type()) == CL(typeid(char const*)))
        RET(boost::any_cast<char const*>(any));
      if (CL(any.type()) == CL(typeid(bool)))
        RET(boost::any_cast<bool>(any));
      if (CL(any.type()) == CL(typeid(int16_t)))
        RET(boost::any_cast<int16_t>(any));
      if (CL(any.type()) == CL(typeid(int32_t)))
        RET(boost::any_cast<int32_t>(any));
      if (CL(any.type()) == CL(typeid(int64_t)))
        RET(boost::any_cast<int64_t>(any));
      if (CL(any.type()) == CL(typeid(uint16_t)))
        RET(boost::any_cast<uint16_t>(any));
      if (CL(any.type()) == CL(typeid(uint32_t)))
        RET(boost::any_cast<uint32_t>(any));
      if (CL(any.type()) == CL(typeid(uint64_t)))
        RET(boost::any_cast<uint64_t>(any));
      if (CL(any.type()) == CL(typeid(long)))
        RET(boost::any_cast<long>(any));
      if (CL(any.type()) == CL(typeid(unsigned long)))
        RET(boost::any_cast<unsigned long>(any));
      if (CL(any.type()) == CL(typeid(long long)))
        RET((int64_t)boost::any_cast<long long>(any));
      if (CL(any.type()) == CL(typeid(unsigned long long)))
        RET((uint64_t)boost::any_cast<unsigned long long>(any));
      if (CL(any.type()) == CL(typeid(float)))
        RET(boost::any_cast<float>(any));
      if (CL(any.type()) == CL(typeid(double)))
        RET(boost::any_cast<double>(any));
      return "null";
    }

    void
    MonitoringServer::MonitorResponseGeneric::pretty_print(
      std::ostream& stream) const
    {
      MonitoringServer::MonitorResponse::pretty_print(stream);
      stream << convert_any(this->result, -1, true) << std::endl;
    }

    /*-----------------------.
    | Monitor Response Peers |
    `-----------------------*/

    MonitoringServer::MonitorResponsePeers::MonitorResponsePeers(Peers peers)
      : MonitoringServer::MonitorResponse(true)
      , peers(std::move(peers))
    {}

    MonitoringServer::MonitorResponsePeers::MonitorResponsePeers(
      elle::serialization::SerializerIn& s)
      : MonitoringServer::MonitorResponse(s)
      , peers(s.deserialize<MonitoringServer::MonitorResponsePeers::Peers>(
          "peers"))
    {}

    void
    MonitoringServer::MonitorResponsePeers::serialize(
      elle::serialization::Serializer& s)
    {
      MonitoringServer::MonitorResponse::serialize(s);
      s.serialize("peers", this->peers);
    }

    static const elle::serialization::Hierarchy<
      MonitoringServer::MonitorResponse>::Register<
        MonitoringServer::MonitorResponsePeers>
          _register_MonitorResponsePeers("peers");

    void
    MonitoringServer::MonitorResponsePeers::pretty_print(
      std::ostream& stream) const
    {
      MonitoringServer::MonitorResponse::pretty_print(stream);
      if (this->peers.size())
      {
        std::string peer_id = "Peer ID";
        auto id_size = this->peers[0].first.size();
        stream << peer_id
               << std::string((id_size - peer_id.size()) + 2, ' ')
               << "Endpoint" << std::endl;
      }
      else
        stream << "No peers" << std::endl;
      for (auto const& element: this->peers)
        stream << element.first << ": " << element.second << std::endl;
    }

    /*----------------------------.
    | Monitor Response Redundancy |
    `----------------------------*/

    MonitoringServer::MonitorResponseRedundancy::MonitorResponseRedundancy(
      std::string description)
      : MonitoringServer::MonitorResponse(true)
      , description(std::move(description))
    {}

    MonitoringServer::MonitorResponseRedundancy::MonitorResponseRedundancy(
      elle::serialization::SerializerIn& s)
      : MonitoringServer::MonitorResponse(s)
      , description(s.deserialize<std::string>("description"))
    {}

    void
    MonitoringServer::MonitorResponseRedundancy::serialize(
      elle::serialization::Serializer& s)
    {
      MonitoringServer::MonitorResponse::serialize(s);
      s.serialize("description", this->description);
    }

    static const elle::serialization::Hierarchy<
      MonitoringServer::MonitorResponse>::Register<
        MonitoringServer::MonitorResponseRedundancy>
          _register_MonitorResponseRedundancy("redundancy");

    void
    MonitoringServer::MonitorResponseRedundancy::pretty_print(
      std::ostream& stream) const
    {
      MonitoringServer::MonitorResponse::pretty_print(stream);
      stream << this->description << std::endl;
    }

    /*-------------.
    | Construction |
    `-------------*/

    MonitoringServer::MonitoringServer(
      std::unique_ptr<reactor::network::Server> server,
      doughnut::Doughnut& owner)
      : _server(std::move(server))
      , _accepter()
      , _owner(owner)
    {
      ELLE_ASSERT(this->_server != nullptr);
      this->_accepter.reset(
        new reactor::Thread(*reactor::Scheduler::scheduler(),
                            "accepter",
                            std::bind(&MonitoringServer::_accept,
                                      std::ref(*this))));
    }

    MonitoringServer::~MonitoringServer()
    {
      if (this->_accepter)
        this->_accepter->terminate_now();
    }

    /*-------.
    | Server |
    `-------*/

    void
    MonitoringServer::_accept()
    {
      elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
      {
        while (true)
        {
          std::shared_ptr<reactor::network::Socket> socket{
            this->_server->accept().release()};
          ELLE_DEBUG("accept connection from %s", *socket);
          scope.run_background(
            elle::sprintf("request %s", *socket),
            [this, socket]
            {
              try
              {
                this->_serve(socket);
              }
              catch (reactor::network::ConnectionClosed const& e)
              {
                ELLE_TRACE("ConnectionClosed: %s", e.backtrace());
              }
              catch (reactor::network::SocketClosed const& e)
              {
                ELLE_TRACE("SocketClosed: %s", e.backtrace());
              }
              catch (...)
              {
                ELLE_ERR("%s: fatal error serving client: %s",
                         this, elle::exception_string());
                throw;
              }
            });
        }
      };
    }

    void
    MonitoringServer::_serve(std::shared_ptr<reactor::network::Socket> socket)
    {
      try
      {
        while (true)
        {
          try
          {
            auto command =
              elle::serialization::json::deserialize<
                MonitoringServer::MonitorQuery>(*socket, false);
            ELLE_TRACE("%s: got command: %s", this, query_str(command.query));
            using Query = MonitoringServer::MonitorQuery::Query;
            std::unique_ptr<MonitoringServer::MonitorResponse> res;
            switch (command.query)
            {
              case Query::Consensus:
                res.reset(new MonitorResponseGeneric(
                  this->_owner.consensus()->information()));
                break;
              case Query::Overlay:
                res.reset(new MonitoringServer::MonitorResponseGeneric(
                  this->_owner.overlay()->information()));
                break;
              case Query::Peers:
                res.reset(new MonitoringServer::MonitorResponsePeers(
                  this->_owner.overlay()->peer_list()));
                break;
              case Query::Redundancy:
                res.reset(new MonitoringServer::MonitorResponseRedundancy(
                  this->_owner.consensus()->redundancy()));
                break;
              case Query::Status:
                res.reset(new MonitoringServer::MonitorResponse(true));
                break;
            }
            if (res)
              elle::serialization::json::serialize(res, *socket, false, false);
          }
          catch (reactor::network::ConnectionClosed const&)
          {
            throw;
          }
          catch (elle::Error const& e)
          {
            std::unique_ptr<MonitoringServer::MonitorResponse> res;
            res.reset(new MonitoringServer::MonitorResponse(
              false, std::string(e.what())));
            elle::serialization::json::serialize(res, *socket, false, false);
          }
        }
      }
      catch (reactor::network::ConnectionClosed const&)
      {}
    }
  }
}
