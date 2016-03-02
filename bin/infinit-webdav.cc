#include <fcntl.h>

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_WINDOWS
#undef stat
#endif

#include <vector>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <reactor/network/tcp-server.hh>
#include <reactor/filesystem.hh>
#include <reactor/http/url.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/filesystem/filesystem.hh>

ELLE_LOG_COMPONENT("webdav");

#include <main.hh>

namespace rn = reactor::network;
namespace rfs = reactor::filesystem;
namespace pt = boost::property_tree;

static std::string slashstrip(std::string const& s)
{
  if (s[s.size()-1] == '/')
    return s.substr(0, s.size()-1);
  else
    return s;
}

typedef std::unordered_map<std::string, std::string> Headers;

struct HTTPQuery
{
  std::string query; // GET, PUT, ...
  std::string url;
  Headers headers;
  elle::Buffer body;
};

struct HTTPReply
{
  int code;
  Headers headers;
  elle::Buffer body;
};

typedef std::function<HTTPReply (HTTPQuery const&)> Handler;
class WebServer: public rn::TCPServer
{
public:
  WebServer(Handler h);
  void listen(int port);
  void process(rn::Socket* s);
private:
  Handler _handler;
};

WebServer::WebServer(Handler h)
: _handler(h)
{}

void WebServer::listen(int port)
{
  rn::TCPServer::listen(port);
  while (true)
  {
    auto socket = accept();
    auto rsock = socket.release();
    new reactor::Thread("http", [this, rsock] { process(rsock);});
  }
}

void WebServer::process(rn::Socket* rsock)
{
  try {
  std::unique_ptr<rn::Socket> sock(rsock);
  while (true)
  {
    auto bheader = sock->read_until("\r\n\r\n");
    HTTPQuery q;
    elle::IOStream is(bheader.istreambuf());
    std::string line;
    std::getline(is, line);
    auto p = line.find_first_of(' ');
    auto p2 = line.find_last_of(' ');
    q.query = line.substr(0, p);
    q.url = reactor::http::url_decode(line.substr(p+1, p2-p-1));
    while (is.good())
    {
      std::getline(is, line);
      line = line.substr(0, line.size()-1);
      if (line.empty())
        break;
      auto p = line.find_first_of(':');
      std::string key = line.substr(0, p);
      boost::algorithm::to_lower(key);
      std::string value = line.substr(p+1);
      while (!value.empty() && value[0] == ' ')
        value = value.substr(1);
      q.headers[key] = value;
    }
    auto it = q.headers.find("content-length");
    if (it != q.headers.end())
    {
      q.body = sock->read(std::stoi(it->second));
    }
    HTTPReply r = _handler(q);
    elle::Buffer reply;
    elle::IOStream os(reply.ostreambuf());
    os << "HTTP/1.1 " << r.code << " YUP\r\n";
    bool has_cl = false;
    for (auto const& h: r.headers)
    {
      std::string key(h.first);
      boost::algorithm::to_lower(key);
      if (key == "content-length")
        has_cl = true;
      os << h.first << ": " << h.second << "\r\n";
    }

    if (!has_cl)
      os << "content-length: " << std::to_string(r.body.size()) << "\r\n";
    os << "\r\n";
    os.flush();
    sock->write(reply);
    if (!r.body.empty())
      sock->write(r.body);
  }
  } catch(std::exception const& e)
  {
    ELLE_WARN("%s", e.what());
  }
}

typedef std::pair<struct stat, std::unordered_map<std::string, std::string>> Infos;

pt::ptree write_xml_stat(std::vector<std::string> const& props,
                         std::string const& name,
                         Infos info,
                         bool silentmissing)
{
  struct stat st = info.first;
  pt::ptree res;
  res.put("DAV:href", name);
  //propstat. {prop, status}
  pt::ptree psok;
  psok.add("DAV:status", "HTTP/1.1 200 OK");
  pt::ptree psfail;
  psfail.add("DAV:status", "HTTP/1.1 404 NOT FOUND");
  if (silentmissing)
  { // We assume silentmissing means PROPFIND without prop list
    std::unordered_map<std::string, std::string> xmlns;
    for (auto const& attr: info.second)
    {
      std::string name = attr.first;
      auto p = name.find_last_of(':');
      if (p == name.npos)
        psok.add("DAV:prop." + name, attr.second);
      else
      {
        auto it = xmlns.find(name.substr(0, p));
        if (it == xmlns.end())
          it = xmlns.insert(std::make_pair(name.substr(0, p),
            std::string("") + char('E' + xmlns.size()))).first;
        psok.add("DAV:prop." + it->second + ":" + name.substr(p+1), attr.second);
      }
    }
    for (auto const& x: xmlns)
      psok.add("DAV:prop.<xmlattr>.xmlns:"+x.second, x.first);
  }
  for (auto const& p: props)
  {
    if (p == "getcontenttype")
    {
      if (S_ISDIR(st.st_mode))
        psok.add("DAV:prop.DAV:getcontenttype", "httpd/unix-directory");
      else
        psok.add("DAV:prop.DAV:getcontenttype", "application/octet-stream");
    }
    else if (p == "getcontentlength")
      psok.add("DAV:prop.DAV:getcontentlength", st.st_size);
    else if (p == "creationdate" || p == "getlastmodified")
    {
      auto tmp = gmtime(&st.st_mtime);
      char stime[100];
      strftime(stime, 100, "%FT%H:%M:%SZ", tmp);
      psok.add("DAV:prop.DAV:" + p, std::string(stime));
      psok.add("DAV:prop.DAV:" + p + ".<xmlattr>.ns0:dt", "dateTime.tz");
    }
    else if (p == "resourcetype")
    {
      if (S_ISDIR(st.st_mode))
        psok.add("DAV:prop.DAV:resourcetype.DAV:collection", "");
      else
        psfail.add("DAV:prop.DAV:resourcetype", "");
    }
    else
    {
      psfail.add("DAV:prop.DAV:" + p, "");
    }
  }
  res.add_child("DAV:propstat", psok);
  if (!silentmissing)
    res.add_child("DAV:propstat", psfail);
  return res;
}

// FIXME: use proper xmlns support!
HTTPReply webdav(HTTPQuery const& q, reactor::filesystem::FileSystem& fs)
{
  ELLE_LOG("query %s %s", q.query, q.url);
  HTTPReply r;
  r.code = 501;
  if (q.query == "OPTIONS")
  {
    r.code = 200;
    r.headers["DAV"] = "1,2";
    // PROPPATCH COPY
    r.headers["Allow"] = "PROPFIND, DELETE, MKCOL, PUT, MOVE,"
      "OPTIONS, GET, HEAD, POST, LOCK, UNLOCK, PROPPATCH";
    r.headers["MS-Author-Via"] = "DAV"; // lighttpd mod_dav sets that
  }
  if (q.query == "PROPFIND")
  {
    r.code = 200;
    int depth = 0;
    try {
      depth = std::stoi(q.headers.at("depth"));
    } catch(...) {}
    pt::ptree tree;
    elle::IOStream is(q.body.istreambuf());
    pt::read_xml(is, tree);
    std::vector<std::string> props;
    bool silentmissing = false;
    try
    {
      auto el = tree.get_child("propfind.prop");
      for (auto const& p: el)
      {
        props.push_back(p.first);
      }
    }
    catch(std::exception const& e)
    {
      props = {"getcontentlength", "getcontenttype", "getlastmodified", "resourcetype"};
      silentmissing = true;
    }
    ELLE_LOG("props queried: %s, depth=%s", props, depth);
    pt::ptree reply;
    auto path = fs.path(slashstrip(q.url));
    struct stat stroot;
    try
    {
      path->stat(&stroot);
    }
    catch(...)
    {
      r.code = 404;
      return r;
    }
    std::unordered_map<std::string, Infos> stats;
    if (depth > 0 && S_ISDIR(stroot.st_mode))
    {
      ELLE_LOG("listing dir");
      path->list_directory([&](std::string const&n, struct stat*) {
          ELLE_LOG("processing %s", n);
          try {
            auto c = path->child(n);
            struct stat st;
            c->stat(&st);
            Infos infos;
            infos.first = st;
            ELLE_DEBUG("got size %s", st.st_size);
            for (auto const& a: c->listxattr())
            {
              infos.second[a] = c->getxattr(a);
            }
            stats[n] = infos;
          }
          catch(std::exception const& e)
          {
            ELLE_TRACE("exception stating %s: %s", n, e.what());
          }
      });
    }
    std::string base = "http://" + q.headers.at("host") + slashstrip(q.url) + "/";
    Infos iroot;
    iroot.first = stroot;
    for (auto const& a: path->listxattr())
      iroot.second[a] = path->getxattr(a);
    auto resp = write_xml_stat(props, base, iroot, silentmissing);
    reply.add_child("DAV:multistatus.DAV:response", resp);
    for (auto const& child: stats)
    {
      ELLE_LOG("child %s", child.first);
      auto resp = write_xml_stat(props, base + child.first, child.second, silentmissing);
      reply.add_child("DAV:multistatus.DAV:response", resp);
    }
    reply.add("DAV:multistatus.<xmlattr>.xmlns:ns0",
      "urn:uuid:c2f41010-65b3-11d1-a29f-00aa00c14882/");
    reply.add("DAV:multistatus.<xmlattr>.xmlns:DAV", "DAV:");
    r.code = 207;
    elle::IOStream os(r.body.ostreambuf());
    pt::write_xml(os, reply, pt::xml_writer_make_settings<std::string>(' ', 1));
    os.flush();
    r.headers["Content-Type"] = "application/xml; charset=\"utf-8\"";
  }
  if (q.query == "PUT")
  {
    auto path = fs.path(slashstrip(q.url));
    auto handle = path->create(O_RDWR | O_CREAT | O_TRUNC, 0666 | S_IFREG);
    if (q.body.size() > 1000000)
    {
      int p = 0;
      while (p < signed(q.body.size()))
      {
        int towrite = std::min(65536, signed(q.body.size()) - p);
        int sz = handle->write(
          elle::WeakBuffer(q.body.mutable_contents() + p, towrite),
          towrite, p);
        p += sz;
      }
    }
    else
      handle->write(q.body, q.body.size(), 0);
    handle->close();
    r.code = 201;
  }
  if (q.query == "GET" || q.query == "HEAD")
  {
    auto path = fs.path(q.url);
    struct stat stroot;
    std::unique_ptr<rfs::Handle> handle;
    try
    {
      path->stat(&stroot);
      handle = path->open(O_RDONLY, 0);
    }
    catch(...)
    {
      r.code = 404; // FIXME could be a 403 too
      return r;
    }
    r.code = 200;
    r.headers["content-type"] = "application/octet-stream";
    if (q.query == "HEAD")
    {
      r.headers["content-length"] = std::to_string(stroot.st_size);
      return r;
    }
    elle::Buffer b;
    b.size(stroot.st_size + 4096);
    int p = 0;
    while (p < stroot.st_size)
    {
      int nr = handle->read(elle::WeakBuffer(b.mutable_contents() + p, 4096),
                            4096, p);
      p += nr;
      if (nr < 4096)
        break;
    }
    b.size(p);
    handle->close();
    r.body = std::move(b);
  }
  if (q.query == "MKCOL")
  {
    try
    {
      auto path = fs.path(slashstrip(q.url));
      path->mkdir(0666);
      r.code = 201;
    }
    catch (std::exception const& e)
    {
      ELLE_TRACE("mkdir failed with %s", e.what());
      r.code = 403;
    }
  }
  if (q.query == "DELETE")
  {
    try
    {
      auto path = fs.path(slashstrip(q.url));
      struct stat stroot;
      path->stat(&stroot);
      if (S_ISDIR(stroot.st_mode))
        path->rmdir();
      else
        path->unlink();
      r.code = 204;
    }
    catch (std::exception const& e)
    {
      ELLE_TRACE("DELETE failed with %s", e.what());
      r.code = 403;
    }
  }
  if (q.query == "MOVE")
  {
    try
    {
      auto path = fs.path(slashstrip(q.url));
      auto dest = slashstrip(q.headers.at("destination"));
      ELLE_DEBUG("move %s -> %s", slashstrip(q.url), dest);
      // dest is full url
      dest = dest.substr(8); // eat http:// and https://
      auto p = dest.find_first_of('/');
      dest = dest.substr(p);
      ELLE_DEBUG("move dest changed to: %s", dest);
      path->rename(dest);
      r.code = 201;
    }
    catch(std::exception const& e)
    {
      ELLE_TRACE("rename failed with %s", e.what());
      r.code = 403;
    }
  }
  if (q.query == "LOCK")
  { // FIXME: dummy implem
    pt::ptree tree;
    elle::IOStream is(q.body.istreambuf());
    pt::read_xml(is, tree);
    pt::ptree info;
    try { info = tree.get_child("D:lockinfo");}
    catch(...) { info = tree.get_child("lockinfo");}
    pt::ptree reply;
    reply.add_child("D:prop.D:lockdiscovery.D:activelock", info);
    reply.add("D:prop.<xmlattr>.xmlns:D", "DAV:");
    reply.add("D:prop.D:lockdiscovery.D:activelock.D:lockroot.D:href",
      "http://" + q.headers.at("host") + q.url);
    reply.add("D:prop.D:lockdiscovery.D:activelock.D:locktoken.D:href",
      "urn:uuid:e71d4fae-5dec-22d6-fea5-00a0c91e6be4");
    reply.add("D:prop.D:lockdiscovery.D:activelock.D:timeout",
      "Second-604800");
    r.headers["lock-token"] = "urn:uuid:e71d4fae-5dec-22d6-fea5-00a0c91e6be4";
    r.code = 200;
    elle::IOStream os(r.body.ostreambuf());
    pt::write_xml(os, reply, pt::xml_writer_make_settings<std::string>(' ', 1));
    os.flush();
    r.headers["Content-Type"] = "application/xml; charset=\"utf-8\"";
  }
  if (q.query == "UNLOCK")
  {
    r.code = 204;
  }
  if (q.query == "PROPPATCH")
  {
    auto path = fs.path(slashstrip(q.url));
    pt::ptree tree;
    pt::ptree reply;
    reply.add("D:multistatus.<xmlattr>.xmlns:D", "DAV:");
    elle::IOStream is(q.body.istreambuf());
    pt::read_xml(is, tree);
    auto root = *tree.begin();
    ELLE_ASSERT(root.first == "D:propertyupdate");
    std::unordered_map<std::string, std::string> xmlns;
    // collect used xmlns
    for (auto const& attr: root.second.get_child("<xmlattr>"))
    {
      std::string attrname = attr.first;
      auto pos = attrname.find_first_of(':');
      if (pos == attrname.npos)
        continue;
      if (attrname != "xmlns:D")
        reply.add("D:multistatus.<xmlattr>." + attrname, attr.second.data());
      if (attrname.substr(0, pos) == "xmlns")
      {
        ELLE_TRACE("xmlns %s -> %s", attrname.substr(pos+1), attr.second.data());
        xmlns[attrname.substr(pos+1)] = attr.second.data();
      }
    }
    reply.add("D:multistatus.D:response.D:href",
      "http://" + q.headers.at("host") + q.url);
    reply.add("D:multistatus.D:response.D:propstat.D:status",
      "HTTP/1.1 200 OK");
    for (auto const& setter: root.second.get_child("D:set.D:prop"))
    {
      reply.add("D:multistatus.D:response.D:propstat.D:prop." + setter.first, "");
      // expand the xmlns
      std::string name = setter.first;
      for (auto const& ns: xmlns)
      {
        auto pos = name.find(ns.first + ":");
        if (pos != name.npos)
        {
          // we leave the ':' in
          name = name.substr(0, pos) + ns.second + name.substr(pos + ns.first.size());
        }
      }
      ELLE_TRACE("%s -> %s", setter.first, name);
      path->setxattr(name, setter.second.data(), 0);
    }
    elle::IOStream os(r.body.ostreambuf());
    pt::write_xml(os, reply, pt::xml_writer_make_settings<std::string>(' ', 1));
    os.flush();
    r.headers["Content-Type"] = "application/xml; charset=\"utf-8\"";
    r.code = 207;
  }
  return r;
}


infinit::Infinit ifnt;

using boost::program_options::variables_map;

void
run(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(name, self);
  std::unordered_map<infinit::model::Address, std::vector<std::string>> hosts;
  bool fetch = args.count("fetch") && args["fetch"].as<bool>();
  if (fetch)
    beyond_fetch_endpoints(network, hosts);
  bool cache = flag(args, option_cache);
  auto cache_ram_size = optional<int>(args, option_cache_ram_size);
  auto cache_ram_ttl = optional<int>(args, option_cache_ram_ttl);
  auto cache_ram_invalidation =
    optional<int>(args, option_cache_ram_invalidation);
  report_action("running", "network", network.name);
  auto model = network.run(
    hosts, true, cache, cache_ram_size, cache_ram_ttl, cache_ram_invalidation,
    flag(args, "async"));
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
    args["volume"].as<std::string>(),
    std::shared_ptr<infinit::model::doughnut::Doughnut>(model.release()));
  reactor::filesystem::FileSystem rfs(std::move(fs), true);

  WebServer ws([&](HTTPQuery const& q) {return webdav(q, rfs);});
  ws.listen(8080);
}

int main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "run",
      "Run",
      &run,
      "--name NETWORK",
      {
        { "fetch", bool_switch(),
            elle::sprintf("fetch endpoints from %s", beyond()).c_str() },
        { "peer", value<std::vector<std::string>>()->multitoken(),
            "peer to connect to (host:port)" },
        { "name", value<std::string>(), "created network name" },
        { "volume", value<std::string>(), "created volume name" },
        { "async", bool_switch(), "Use asynchronious operations" },
        option_cache,
        option_cache_ram_size,
        option_cache_ram_ttl,
        option_cache_ram_invalidation,
      },
    },
  };
  return infinit::main("Infinit webDAV adapter", modes, argc, argv);
}
