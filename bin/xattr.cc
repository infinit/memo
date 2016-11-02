#include <iostream>
#include <sstream>
#include <string>

#include <sys/xattr.h>

#include <unistd.h>

int
main(int argc, char** argv)
{
  // sleep(30);
  std::string k = argv[1];
  std::cout << "key: " << k << std::endl;
  std::string f = argv[2];
  std::cout << "path: " << f << std::endl;
  char buf[4096];
  int sz = getxattr(f.c_str(), k.c_str(), buf, 4095, 0, XATTR_NOFOLLOW);
  if (sz < 0)
  {
    std::cout << "error: " << std::strerror(errno) << std::endl;
    return 1;
  }
  else
  {
    buf[sz] = 0;
    std::stringstream ss;
    ss.str(buf);
    std::cout << "value: " << ss.str() << std::endl;
  }
  return 0;
}
