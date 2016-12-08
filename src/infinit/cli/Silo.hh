#pragma once

#include <boost/optional.hpp>

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Silo
      : public Entity<Silo>
    {
    public:
      Silo(Infinit& cli);
      using Modes = decltype(elle::meta::list(cli::create,
                                              cli::delete_,
                                              cli::export_,
                                              cli::import,
                                              cli::list));
      // Create
      Mode<decltype(binding(modes::mode_create,
                            name,
                            description = boost::none,
                            capacity = boost::none,
                            output = boost::none,
                            dropbox,
                            filesystem,
                            gcs,
                            google_drive,
                            ssh,
                            s3,
                            account = boost::none,
                            bucket = boost::none,
                            endpoint = "amazonaws.com",
                            host = boost::none,
                            region = boost::none,
                            path = boost::none,
                            storage_class = boost::none))>
      create;
      void
      mode_create(std::string name,
                  boost::optional<std::string> description,
                  boost::optional<std::string> capacity,
                  boost::optional<std::string> output,
                  bool dropbox,
                  bool filesystem,
                  bool gcs,
                  bool google_drive,
                  bool ssh,
                  bool s3,
                  boost::optional<std::string> account,
                  boost::optional<std::string> bucket,
                  std::string endpoint,
                  boost::optional<std::string> host,
                  boost::optional<std::string> region,
                  boost::optional<std::string> path,
                  boost::optional<std::string> storage_class);
      Mode<decltype(binding(modes::mode_delete,
                            name,
                            clear_content = false,
                            purge = false))>
      delete_;
      void
      mode_delete(std::string name,
                  bool clear,
                  bool purge);
      Mode<decltype(binding(modes::mode_export,
                            name,
                            output = boost::none))>
      export_;
      void
      mode_export(std::string name,
                  boost::optional<std::string> output);
      Mode<decltype(binding(modes::mode_import,
                            input = boost::none))>
      import;
      void
      mode_import(boost::optional<std::string> input);
      Mode<decltype(binding(modes::mode_list))>
      list;
      void
      mode_list();
    };
  }
}
