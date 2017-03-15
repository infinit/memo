#include <infinit/model/MonitoringServer.hh>

#include <sstream>

#include <boost/filesystem.hpp>

#include <elle/serialization/json.hh>

#include <elle/reactor/network/exception.hh>
#include <elle/reactor/Scope.hh>

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
        case Query::Stats:
          return "stats";
        case Query::Status:
          return "status";
      }
      elle::unreachable();
    }

    static
    MonitoringServer::MonitorQuery::Query
    query_val(std::string const& query_str)
    {
      using Query = MonitoringServer::MonitorQuery::Query;
      if (query_str == "stats")
        return Query::Stats;
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
      boost::optional<std::string> error,
      boost::optional<elle::json::Object> result)
      : success(success)
      , error(std::move(error))
      , result(std::move(result))
    {}

    MonitoringServer::MonitorResponse::MonitorResponse(
      elle::json::Object response)
      : success(boost::any_cast<bool>(response["success"]))
      , error(boost::none)
      , result(boost::none)
    {
      if (response.count("error"))
        this->error = boost::any_cast<std::string>(response["error"]);
      response.erase("success");
      response.erase("error");
      if (response.size())
        this->result = std::move(response);
    }

    elle::json::Object
    MonitoringServer::MonitorResponse::as_object() const
    {
      elle::json::Object res;
      res["success"] = this->success;
      if (this->error)
        res["error"] = this->error.get();
      if (this->result)
        for (auto const& pair: this->result.get())
          res[pair.first] = pair.second;
      return res;
    }

    void
    MonitoringServer::MonitorResponse::print(std::ostream& stream) const
    {
      stream << "MonitorResponse(" << (this->success ? "success" : "fail");
      if (this->error)
        stream << ", error: " << this->error.get();
      stream << ")";
    }

    /*-------------.
    | Construction |
    `-------------*/

    MonitoringServer::MonitoringServer(
      std::unique_ptr<elle::reactor::network::Server> server,
      doughnut::Doughnut& owner)
      : _server(std::move(server))
      , _accepter()
      , _owner(owner)
    {
      ELLE_ASSERT_NEQ(this->_server, nullptr);
      ELLE_DEBUG("listening on: %s", *this->_server);
      this->_accepter.reset(
        new elle::reactor::Thread(*elle::reactor::Scheduler::scheduler(),
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
      elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
      {
        while (true)
        {
          std::shared_ptr<elle::reactor::network::Socket> socket{
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
              catch (elle::reactor::network::ConnectionClosed const& e)
              {
                ELLE_TRACE("ConnectionClosed: %s", e.backtrace());
              }
              catch (elle::reactor::network::SocketClosed const& e)
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
    MonitoringServer::_serve(std::shared_ptr<elle::reactor::network::Socket> socket)
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
            std::unique_ptr<MonitoringServer::MonitorResponse> response;
            switch (command.query)
            {
              case Query::Stats:
              {
                auto res = elle::json::Object{
                  {"consensus", this->_owner.consensus()->stats()},
                  {"overlay", this->_owner.overlay()->stats()},
                  {"peers", this->_owner.overlay()->peer_list()},
                  {"protocol", elle::sprintf("%s", this->_owner.protocol())},
                  {"redundancy", this->_owner.consensus()->redundancy()},
                };
                response.reset(new MonitoringServer::MonitorResponse(
                  true, {}, res));
                break;
              }
              case Query::Status:
                response.reset(new MonitoringServer::MonitorResponse(true));
                break;
            }
            if (response)
              elle::json::write(*socket, response->as_object());
          }
          catch (elle::reactor::network::ConnectionClosed const&)
          {
            throw;
          }
          catch (elle::Error const& e)
          {
            MonitoringServer::MonitorResponse response(
              false, std::string(e.what()));
            elle::json::write(*socket, response.as_object());
          }
        }
      }
      catch (elle::reactor::network::ConnectionClosed const&)
      {}
    }
  }
}
