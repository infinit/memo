#include <reactor/network/resolve.hh>

static
infinit::model::Endpoints
endpoints_from_file(boost::filesystem::path const& path)
{
  boost::filesystem::ifstream f;
  ifnt._open_read(f, path, "", "port file");
  if (!f.good())
    elle::err("unable to open for reading: %s", path);
  infinit::model::Endpoints res;
  for (std::string line; std::getline(f, line); )
    if (line.length())
      res.emplace_back(infinit::model::Endpoint(line));
  return res;
}

static
void
port_to_file(uint16_t port, boost::filesystem::path const& path_)
{
  boost::filesystem::ofstream f;
  boost::filesystem::path path(
    path_ == path_.filename() ? boost::filesystem::absolute(path_) : path_);
  ifnt._open_write(f, path, "", "port file", true);
  f << port << std::endl;
}

static
void
endpoints_to_file(std::vector<reactor::network::TCPServer::EndPoint> endpoints,
                  boost::filesystem::path const& path_)
{
  boost::filesystem::ofstream f;
  boost::filesystem::path path(
    path_ == path_.filename() ? boost::filesystem::absolute(path_) : path_);
  ifnt._open_write(f, path, "", "endpoint file", true);
  for (auto const& ep: endpoints)
    f << ep << std::endl;
}
