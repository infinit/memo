#pragma once

#include <elle/json/json.hh>
#include <elle/Printable.hh>

#include <elle/reactor/network/server.hh>
#include <elle/reactor/network/socket.hh>

#include <memo/model/doughnut/fwd.hh>
#include <memo/serialization.hh>

namespace memo
{
  namespace model
  {
    class MonitoringServer
    {
    public:
      struct MonitorQuery
        : public elle::Printable
      {
        enum class Query
          : std::int8_t
        {
          Stats = 1, // Information about the overlay and consensus algorithm.
          Status,    // Check if the network is running.
        };

        MonitorQuery(Query query);
        MonitorQuery(elle::serialization::SerializerIn& s);

        Query query;

        void
        serialize(elle::serialization::Serializer& s);
        void
        print(std::ostream& stream) const override;
      };

    public:
      struct MonitorResponse
        : public elle::Printable
      {
        MonitorResponse(bool success,
                        boost::optional<std::string> error = {},
                        boost::optional<elle::json::Object> result = {});
        MonitorResponse(elle::json::Object response);

        bool success;
        boost::optional<std::string> error;
        boost::optional<elle::json::Object> result;

        elle::json::Object
        as_object() const;
        void
        print(std::ostream& stream) const;
      };

      /*-------------.
      | Construction |
      `-------------*/
    public:
      MonitoringServer(std::unique_ptr<elle::reactor::network::Server> server,
                       doughnut::Doughnut& owner);
      ~MonitoringServer();

      ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::network::Server>, server);
      ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::Thread>, accepter);

    private:
      ELLE_ATTRIBUTE(doughnut::Doughnut&, owner);

      /*-------.
      | Server |
      `-------*/
    private:
      void
      _accept();
      void
      _serve(std::shared_ptr<elle::reactor::network::Socket> socket);
    };
  }
}
